# CPR External AED Shock Controller

Ultra-low-latency, deterministic external AED Shock Unit firmware running on **Zephyr RTOS v4.x** for the **ESP32-C3 Super Mini**.

> **Firmware v4.0.0:** the mobile App now connects **directly** to the AED (and directly to the Manikin).
> The AED and the Manikin no longer talk to each other.
> `bin/zephyr.bin` is the OLD firmware: rebuild before flashing (see section 6).
> This version has not been compiled or run yet; test with nRF Connect first.

---

## 1. System Overview

```
                  ┌─────────────────────────┐
                  │    Mobile Application   │
                  │   (BLE central, hub)    │
                  └───────┬─────────┬───────┘
                          │         │
            passkey, bonded, NUS    │   passkey, bonded, NUS
                          ▼         ▼
            ┌──────────────────┐   ┌──────────────────┐
            │  AED Controller  │   │  CPR Manikin     │
            │  "AED_001"       │   │  "CPR_106"       │
            └──────────────────┘   └──────────────────┘
```

The App decides when the shock button may be used: it checks the pads on the Manikin, then sends `S1` to the AED. A shock always needs **two physical presses** (power button, then shock button).

---

## 2. Session Flow

| Step | Trigger | `power` | `shock` | LED |
| :---: | :--- | :---: | :---: | :--- |
| 0 | App connected | 0 | 0 | off (blue breathing while no App) |
| 1 | App sends **`P1`** | 2 (on) | 0 | solid yellow |
| 2 | App checks the pads, sends **`S1`** | 2 | 1 (waiting for press) | solid yellow |
| 3 | User presses the **shock button** | 2 | 2 (in progress) | yellow 5 s, then red strobe 5 s (+ 1 ms sync pulse on GPIO 7) |
| 4 | Sequence ends | 2 | 3 (delivered) | solid green |
| 5 | App sends **`P0`** | 0 | 0 | off |

Rules:
* `P1` turns the AED power directly ON (state 2, solid yellow LED).
* `S1` is accepted only when `power = 2`; otherwise it is ignored and the current state is sent again.
* `S0` disables the shock button (only while `shock = 1`).
* `P0` forces power 0 and shock 0 from any state and stops a running sequence.
* **GPIO 6 (Monitor only):** acts as a non-intrusive sensor/trigger monitor. When triggered, it emits event `0x0B` to inform the App and logs without resetting the system state.
* **Fail-safe:** if the App link is lost, the AED returns to power 0, shock 0.

---

## 3. Commands (App -> AED, NUS RX, plain ASCII)

| Command | Action |
| :--- | :--- |
| **`C`** or **`?`** | Announce request: AED replies with the 20-byte ANNOUNCE packet |
| **`P0`** / **`P1`** | Power button disable / enable |
| **`S0`** / **`S1`** | Shock button disable / enable |

Unknown commands are ignored.

---

## 4. Events (AED -> App, NUS TX notify, 15-byte packet)

Events are sent **only when a value changes**. When the App enables notify, both values are sent once (snapshot). After a command that changes nothing (for example an ignored `S1`), the current value is sent again.

| Type | Event | Value (byte 10) |
| :---: | :--- | :--- |
| `0x09` | power | `0` button off, `1` waiting for press, `2` power on |
| `0x0A` | shock | `0` button off, `1` waiting for press, `2` in progress, `3` delivered |
| `0x01` | announce (20 bytes) | reply to `C` / `?`: deviceID, name `001`, device type `2` (AED), battery `-1` (not measured) |

Packet layout (same as the Manikin): `C9 C2 | ver 06 | type | timestamp (4) | id (2) | value | deviceID (3) | XOR checksum`.

---

## 5. BLE Security & Advertising

* LE Secure Connections only, **fixed passkey** (`AED_BLE_PASSKEY` in `src/aed_config.h`, default `123456`). **Change it per unit** and print it on the label.
* Every GATT attribute needs an authenticated + encrypted link. One bonded App, bond stored in flash.
* Advertising: name `AED_001`, manufacturer data `C4 C9 <state>` (`00` available, `01` App connected), NUS service UUID in the scan response.
* Not implemented yet: button-triggered pairing mode and advertising only to the bonded App.

Nordic UART Service UUIDs: service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`, RX (write) `...0002...`, TX (notify) `...0003...`.

---

## 6. Build & Flash Instructions

```powershell
$env:ZEPHYR_BASE="C:\zephyrproject\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR="C:\zephyr-sdk-1.0.1"
cmake -G Ninja -B build -S . -DBOARD=esp32c3_devkitm -DCMAKE_C_FLAGS="-BC:/zb/as_bin/ -fno-lto -fno-use-linker-plugin"
ninja -C build zephyr/zephyr.elf
python -m esptool --chip esp32c3 elf2image --ram-only-header --flash-mode dio --flash-freq 80m --flash-size 4MB -o bin/zephyr.bin build/zephyr/zephyr.elf
python -m esptool --chip esp32c3 --port COM8 --baud 921600 write_flash 0x0 bin/zephyr.bin
```

Bond storage needs the board's `storage_partition` (flash). If the build complains about it, check the flash layout of `esp32c3_devkitm`.

---

## 7. Hardware GPIO Assignments (ESP32-C3 Super Mini)

| Pin | Function | Description |
| :--- | :--- | :--- |
| **GPIO 9** | Shock button | Active-LOW momentary button, works only when `shock = 1` |
| **GPIO 6** | Monitor / Trigger | Active-LOW **monitor** input; emits event `0x0B` when triggered without resetting state |
| **GPIO 7** | Sync pulse out | 1 ms pulse at the start of the red stage |
| **GPIO 8** | Status LED | Onboard blue LED (active-LOW) |
| **GPIO 3 / 4 / 5** | RGB LED | Red / Green / Blue (common cathode) |

---

## 8. Repository Layout

```
CPR_Zephyr_AED_Controller/
├── bin/zephyr.bin                 ← OLD precompiled binary (rebuild!)
├── src/
│   ├── main.c                     ← State machine (power / shock), event queue, shock sequence
│   ├── aed_config.h               ← Pinout, timings, passkey, advertising codes
│   ├── aed_protocol.h             ← Event / announce packet layouts
│   ├── aed_ble.c / .h             ← BLE peripheral: NUS, security, command parser, notify
│   ├── aed_button.c / .h          ← Power + shock button drivers (debounce, ISR callbacks)
│   └── aed_led.c / .h             ← RGB LED state machine & PWM driver
├── app.overlay, CMakeLists.txt, prj.conf
└── README.md
```

---

## 9. Related Firmware

* **CPR Manikin MCU (Zephyr RTOS):** `CPR_BLE_Zephyr_Controller_No_LED-main` (commands `C ? A0 A1 B0 B1 H0 H1`)
