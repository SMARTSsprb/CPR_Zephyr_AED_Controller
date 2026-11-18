# CPR External AED Shock Controller

Ultra-low-latency, deterministic external AED Shock Unit firmware running on **Zephyr RTOS v4.x** (with companion Arduino sketch) for the **ESP32-C3 Super Mini**.

---

## 1. System Overview

The external AED unit simulates clinical defibrillator workflows for CPR training. It communicates wirelessly with the **CPR Manikin MCU** (`CPR_BLE_Prototype_c3`) exclusively via the **ESP-NOW protocol** (2.4 GHz raw RF frames) for sub-millisecond deterministic latency.

```
┌─────────────────────────────────┐   ESP-NOW (2.4 GHz)   ┌─────────────────────────────────┐
│     AED Shock Controller        │ ────────────────────► │          CPR Manikin            │
│   ESP32-C3 Super Mini (Zephyr)  │   Direct MAC Frame    │   ESP32-C3 Super Mini (Zephyr)  │
│   MAC: Dynamic STA MAC          │                       │   MAC: E8:3D:C1:82:1B:F0        │
└─────────────────────────────────┘                       └─────────────────────────────────┘
```

---

## 2. RGB LED Indication System (Clinical Resuscitation Flow)

The AED unit provides intuitive visual feedback using a high-visibility RGB LED indicator matching the clinical resuscitation rationale:

```
[Mode 01: Waiting]  ──(Peer Registered)──►  [Mode 02: Peer Active]
   (Deep Blue)                                 (Emerald Green)
                                                     │
                                             (Button Pressed)
                                                     │
                                                     ▼
                                            [Mode 03: Alert Burst] (Amber Triple Strobe)
                                                     │ (~360ms auto-transition)
                                                     ▼
                                            [Mode 04: Shock Giving] (Red 5 Hz Hazard 5s)
                                                     │ (1ms Sync Pulse on GPIO 7)
                                                     ▼
[Mode 02: Peer Active]  ◄──(1.0s Latch)───  [Mode 05: Shock Off] (Pure Green Latch)
```

| Mode | Visual Feedback | Color & Channel | Pattern & Timing | Clinical Meaning / Resuscitation Rationale |
| :---: | :--- | :--- | :--- | :--- |
| **01** | **WAITING FOR PEER** | 🔵 **Deep Blue** | **Smooth Breathing Rhythm**<br>150-step organic curve (0% $\rightarrow$ 100% $\rightarrow$ 0% over 3.0s @ 20ms tick) | **Alive & Seeking Peer:** System booted; looking for Manikin receiver over ESP-NOW. |
| **02** | **PEER ACTIVE & READY** | 🟢 **Emerald Green** | **Continuous Steady Glow**<br>100% Constant Solid ON | **Safe Standby & Telemetry Synced:** Peer registered; zero visual distraction while preparing scenario. |
| **03** | **SHOCK ON (ALERT)** | 🟡 **Warm Amber** | **One-Time Triple Strobe Burst**<br>3x [60ms ON / 60ms OFF] (~360ms total burst) | **Shock Advised:** Capacitors charging; automatically transitions to Mode 04 upon burst completion. |
| **04** | **SHOCK GIVING (ARMED)** | 🔴 **Urgent Red** | **Rapid Hazard Strobe**<br>5 Hz cadence (100ms ON / 100ms OFF) for 5.0 seconds | **High-Voltage Hazard:** *"STAND CLEAR — DELIVERING SHOCK!"* Fires **1ms hardware sync pulse** on GPIO 7. |
| **05** | **SHOCK OFF (DELIVERED)** | 🟢 **Pure Green** | **1.0s Confirmation Latch**<br>1000ms Solid Green, then seamlessly returns to Mode 02 | **Energy Safely Discharged:** Safe to touch patient — resume CPR compressions. |

---

## 3. Hardware GPIO Assignments (ESP32-C3 Super Mini)

| Pin | Function | Electrical Type | Description |
| :--- | :--- | :--- | :--- |
| **GPIO 9** | **Shock Button** | Input (Internal Pull-Up) | Active-LOW momentary shock button (Onboard BOOT or external) |
| **GPIO 7** | **Sync Pulse Out** | Output (Active-HIGH) | 1ms hardware synchronization pulse fired to Manikin at start of Mode 04 |
| **GPIO 8** | **Status LED** | Output (Active-LOW) | Onboard blue LED synchronized with breathing peak and strobe states |
| **GPIO 3** | **RGB Red** | Output (Active-HIGH) | Common Cathode Red channel |
| **GPIO 4** | **RGB Green** | Output (Active-HIGH) | Common Cathode Green channel |
| **GPIO 5** | **RGB Blue** | Output (Active-HIGH) | Common Cathode Blue channel |
| **GPIO 6** | **Power Switch** | Input / Hardware Cut | Hardware inline power switch (Option 1) or logic pin (Option 2) |

---

## 4. ESP-NOW Wireless Telemetry Protocol

### Packet Structure (Packed C Struct)

```c
typedef struct __attribute__((packed)) {
    uint8_t  cmd;    /* 0=AED_OFF, 1=AED_ON, 2=SHOCK_ON, 3=SHOCK_OFF */
    uint8_t  stage;  /* 0=idle, 1=alert burst, 2=delivering (5s), 3=complete */
    uint32_t ts;     /* Millisecond timestamp (k_uptime_get_32 / millis) */
} aed_espnow_pkt_t;
```

### Command Sequence

| Command | `cmd` | `stage` | Timing / Trigger | Action |
| :--- | :---: | :---: | :--- | :--- |
| **`AED_ON`** | `1` | `0` | On power-on / boot | Registers peer and transitions LED to **Mode 02 (Solid Green)** |
| **`SHOCK_ON` (Alert)** | `2` | `1` | Shock Button Pressed | Initiates Mode 03 Warm Amber Triple Strobe (~360ms) |
| **`SHOCK_ON` (Delivering)**| `2` | `2` | Auto-transition (+360ms) | Fires 1ms sync pulse on GPIO 7 and runs 5 Hz Red Strobe for 5.0s |
| **`SHOCK_OFF` (Complete)** | `3` | `3` | Delivery finished (+5.0s) | Initiates Mode 05 Pure Green 1.0s latch, then returns to Mode 02 |
| **`AED_OFF`** | `0` | `0` | On power switch OFF | Unit powered down; LED turned off |

* **Target Manikin Wi-Fi STA MAC:** `E8:3D:C1:82:1B:F0`
* **Wi-Fi Channel:** Channel 1 (2.4 GHz)

---

## 5. Build & Flash Instructions

### Option A: Flash Precompiled Zephyr RTOS Binary (Recommended)

A precompiled, production-ready Zephyr RTOS binary is provided at `bin/zephyr.bin`:

```powershell
python -m esptool --chip esp32c3 --port COM8 --baud 921600 write_flash 0x0 bin/zephyr.bin
```

### Option B: Build from Zephyr RTOS Source

Requirements: Zephyr SDK, CMake, Ninja, and Python 3.

```powershell
# 1. Configure CMake with devicetree overlay
cmake -G Ninja -B build -S . -DBOARD=esp32c3_devkitm

# 2. Compile application and link zephyr.elf
ninja -C build zephyr/zephyr.elf

# 3. Create ESP32-C3 bootable image
python -m esptool --chip esp32c3 elf2image --ram-only-header --flash-mode dio --flash-freq 80m --flash-size 4MB -o bin/zephyr.bin build/zephyr/zephyr.elf

# 4. Flash to board
python -m esptool --chip esp32c3 --port COM8 --baud 921600 write_flash 0x0 bin/zephyr.bin
```

### Option C: Arduino IDE Firmware

The repository also includes the Arduino-compatible implementation located in [`CPR_AED_ESP_NOW/`](CPR_AED_ESP_NOW/):

1. Open `CPR_AED_ESP_NOW/CPR_AED_ESP_NOW.ino` in Arduino IDE.
2. Select Board: **ESP32C3 Dev Module**.
3. Flash with settings outlined in [`ARDUINO_BOARD_SETTINGS.md`](CPR_AED_ESP_NOW/ARDUINO_BOARD_SETTINGS.md).

---

## 6. Repository Layout

```
CPR_Zephyr_AED_Controller/
├── bin/
│   └── zephyr.bin                 ← Verified precompiled Zephyr binary (544 KB)
├── src/
│   ├── main.c                     ← Application entry point & state machine
│   ├── aed_config.h               ← Pinout, timing & protocol definitions
│   ├── aed_led.c / .h             ← 5-Mode RGB LED state machine & PWM driver
│   ├── aed_button.c / .h          ← Shock button debounce & sync pulse driver
│   └── aed_espnow.c / .h          ← ESP-NOW wireless transmitter subsystem
├── CPR_AED_ESP_NOW/
│   ├── CPR_AED_ESP_NOW.ino        ← Arduino firmware (100% parity with Zephyr)
│   └── ARDUINO_BOARD_SETTINGS.md  ← Arduino IDE configuration guide
├── app.overlay                    ← Devicetree overlay for ESP32-C3 Super Mini
├── CMakeLists.txt                 ← Zephyr build definition
├── prj.conf                       ← Zephyr kernel & Wi-Fi configuration
├── aed_rgb_led_indications.png    ← Clinical visual feedback timing diagram
└── README.md                      ← Documentation
```

---

## 7. Related Firmware

- **CPR Manikin MCU (Zephyr RTOS):** [SMARTSsprb/CPR_BLE_Zephyr_Controller_No_LED](https://github.com/SMARTSsprb/CPR_BLE_Zephyr_Controller_No_LED)
