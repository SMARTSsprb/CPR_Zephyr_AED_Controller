# CPR External AED Shock Controller

Ultra-low-latency, deterministic external AED Shock Unit firmware running on **Zephyr RTOS v4.x** for the **ESP32-C3 Super Mini**.

---

## 1. System Overview & BLE Architecture

The external AED unit simulates clinical defibrillator workflows for CPR training. It operates exclusively using **Bluetooth Low Energy (BLE)** in a dedicated Peripheral / Broadcaster topology designed for multi-AED training environments:

```
                      ┌─────────────────────────┐
                      │    Mobile Application   │
                      │     (Tablet / Phone)    │
                      └────────────┬────────────┘
                                   │
              Discovers Broadcast  │  Assigns AED_001
              (No Direct Pairing)  │  to Manikin via '@'
                                   ▼
┌─────────────────────────┐   BLE Link    ┌─────────────────────────┐
│     AED Controller      │ ◄───────────► │       CPR Manikin       │
│  (BLE Peripheral / Adv) │ (NUS Service) │ (BLE Central for AED,   │
│   Device: "AED_001"     │               │  Peripheral for App)    │
└─────────────────────────┘               └─────────────────────────┘
```

### Key Architectural Rules:
1. **Zero Data Transmission between AED & Application:** The AED only emits advertising packets (`AED_001` with Manufacturer Data telemetry). The Application passively scans and discovers available AEDs without pairing or bonding.
2. **Application Manikin-AED Assignment:** In the Application UI, the user selects and assigns an AED to a specific Manikin using the `@` protocol (`@LIST`, `@SET:<TOKEN>`).
3. **Manikin-to-AED Pairing:** Once assigned, the CPR Manikin connects to the AED over BLE as a Central device and escalates security to `BT_SECURITY_L2`.
4. **Deterministic Visual Feedback:** Upon connection, the AED transitions from Mode 01 (Deep Blue Breathing) to Mode 02 (Solid Emerald Green).
5. **Shock Execution:** Triggered via physical shock button (GPIO 9) or via Manikin BLE write (`SHOCK_ON` / `'E'` / `'K1'`), firing a 1 ms hardware synchronization pulse on GPIO 7.

---

## 2. RGB LED Indication System (Clinical Resuscitation Flow)

The AED unit provides intuitive visual feedback using a high-visibility RGB LED indicator matching the clinical resuscitation rationale:

```
[Mode 01: Waiting]  ──(Manikin Connected)──►  [Mode 02: Peer Active]
   (Deep Blue)                                   (Emerald Green)
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
[Mode 02: Peer Active]  ◄──(1.0s Latch)─────  [Mode 05: Shock Off] (Pure Green Latch)
```

| Mode | Visual Feedback | Color & Channel | Pattern & Timing | Clinical Meaning / Resuscitation Rationale |
| :---: | :--- | :--- | :--- | :--- |
| **01** | **WAITING FOR PEER** | 🔵 **Deep Blue** | **Smooth Breathing Rhythm**<br>150-step organic curve (0% $\rightarrow$ 100% $\rightarrow$ 0% over 3.0s @ 20ms tick) | **Alive & Seeking Peer:** System booted; advertising `AED_001` and awaiting Manikin connection. |
| **02** | **PEER ACTIVE & READY** | 🟢 **Emerald Green** | **Continuous Steady Glow**<br>100% Constant Solid ON | **Safe Standby & Telemetry Synced:** Manikin connected; zero visual distraction while preparing scenario. |
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

## 4. BLE Wireless Protocol & Advertising Telemetry

### Advertising Data Structure
* **Device Complete Name:** `"AED_001"` (Configurable in `aed_config.h` / `prj.conf`)
* **Manufacturer Specific Data (3 bytes):**
  * `Byte 0-1`: Company ID `0xC9C4` (Little-Endian: `0xC4`, `0xC9`)
  * `Byte 2`: State Code:
    * `0x00`: **Available / Idle** (Mode 01 - Waiting for Manikin)
    * `0x01`: **Paired / Active** (Mode 02 - Manikin Connected)
    * `0x02`: **Shock Active** (Mode 03 / 04 - Delivering Shock)

### Nordic UART Service (NUS) GATT Characteristics
* **Service UUID:** `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
* **RX Characteristic (Write Without Response):** `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
  * Accepts `'E'`, `'K1'`, or `"SHOCK_ON"` $\rightarrow$ initiates shock sequence.
  * Accepts `'D'`, `'K0'`, or `"SHOCK_OFF"` $\rightarrow$ disarms / completes shock sequence.
  * Accepts `'A1'` or `"AED_ON"` $\rightarrow$ wakes / enables AED controller.
  * Accepts `'A0'` or `"AED_OFF"` $\rightarrow$ puts AED controller to sleep.
* **TX Characteristic (Notify):** `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
  * Streams 5-byte companion frames: `[0xAE, 0x01, <event>, <seq_lo>, <seq_hi>]`
  * Events: `0x00` (`AED_HELLO`), `0x01` (`AED_POWER_OFF`), `0x02` (`AED_POWER_ON`), `0x03` (`AED_SIM_DELIVERED`).

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
# 1. Configure CMake with Zephyr environment
$env:ZEPHYR_BASE="C:\zephyrproject\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR="C:\zephyr-sdk-1.0.1"
cmake -G Ninja -B build -S . -DBOARD=esp32c3_devkitm -DCMAKE_C_FLAGS="-BC:/zb/as_bin/ -fno-lto -fno-use-linker-plugin"

# 2. Compile application and link zephyr.elf
ninja -C build zephyr/zephyr.elf

# 3. Create ESP32-C3 bootable image
python -m esptool --chip esp32c3 elf2image --ram-only-header --flash-mode dio --flash-freq 80m --flash-size 4MB -o bin/zephyr.bin build/zephyr/zephyr.elf

# 4. Flash to board
python -m esptool --chip esp32c3 --port COM8 --baud 921600 write_flash 0x0 bin/zephyr.bin
```

---

## 6. Repository Layout

```
CPR_Zephyr_AED_Controller/
├── bin/
│   └── zephyr.bin                 ← Verified precompiled Zephyr BLE binary (433 KB)
├── src/
│   ├── main.c                     ← Application entry point & state machine
│   ├── aed_config.h               ← Pinout, timing & BLE protocol definitions
│   ├── aed_led.c / .h             ← 5-Mode RGB LED state machine & PWM driver
│   ├── aed_button.c / .h          ← Shock button debounce & sync pulse driver
│   └── aed_ble.c / .h             ← BLE Peripheral & NUS GATT service subsystem
├── app.overlay                    ← Devicetree overlay for ESP32-C3 Super Mini
├── CMakeLists.txt                 ← Zephyr build definition
├── prj.conf                       ← Zephyr kernel & BLE stack configuration
├── aed_rgb_led_indications.png    ← Clinical visual feedback timing diagram
└── README.md                      ← Documentation
```

---

## 7. Related Firmware

* **CPR Manikin MCU (Zephyr RTOS):** [SMARTSsprb/CPR_BLE_Zephyr_Controller_No_LED](https://github.com/SMARTSsprb/CPR_BLE_Zephyr_Controller_No_LED)
