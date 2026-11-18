# CPR External AED Shock Unit

**ESP32-C3 Super Mini — Arduino / ESP-NOW Firmware**

## Overview

This is the firmware for the **external AED shock unit** in the CPR training system.
It communicates wirelessly with the **Manikin MCU** (`CPR_BLE_Prototype_c3`) using
**ESP-NOW protocol exclusively** — no Bluetooth Low Energy (BLE) is used.

```
┌──────────────────────┐  ESP-NOW  ┌──────────────────────────┐
│   AED MCU            │ ────────► │   Manikin MCU             │
│   ESP32-C3           │           │   ESP32-C3 (BLE + ESP-NOW)│
│   CPR_AED_ESP_NOW    │           │   CPR_BLE_Prototype_c3    │
└──────────────────────┘           └──────────────────────────┘
```

## Shock Sequence (triggered by GPIO 9 button)

| Stage | Duration | LED               | ESP-NOW Packet          |
|-------|----------|-------------------|-------------------------|
| 1     | 5s       | 🟡 Solid Yellow   | `SHOCK_ON  stage=1`     |
| 2     | 5s       | 🔴 Red Strobe     | `SHOCK_ON  stage=2`     |
| 3     | 1s       | 🟢 Solid Green    | `SHOCK_OFF stage=3`     |
| —     | Standby  | 🔵 Blue Breathing | `AED_ON` (sent at boot) |

> At Stage 2 start: a **1ms hardware sync pulse** is also fired on **GPIO 7** to the Manikin.

## GPIO Map

| GPIO | Function         | Description                       |
|------|------------------|-----------------------------------|
| 9    | Shock Button     | Active-LOW, internal pull-up      |
| 6    | Power Switch     | Option 1: hardware power cut      |
| 8    | Onboard LED      | Active-LOW blue status LED        |
| 7    | Sync Pulse Out   | 1ms HIGH pulse at Stage 2 start   |
| 3    | RGB Red          | Common Cathode (Active-HIGH)      |
| 4    | RGB Green        | Common Cathode (Active-HIGH)      |
| 5    | RGB Blue         | Common Cathode (Active-HIGH)      |

## ESP-NOW Packet

```c
typedef struct __attribute__((packed)) {
    uint8_t  cmd;    // 0=AED_OFF, 1=AED_ON, 2=SHOCK_ON, 3=SHOCK_OFF
    uint8_t  stage;  // 0=idle, 1=yellow, 2=red, 3=done
    uint32_t ts;     // millis() timestamp
} aed_espnow_pkt_t;
```

**Target Manikin MAC:** `E8:3D:C1:82:1B:F0` (Wi-Fi STA)  
**ESP-NOW Channel:** 1

## Build & Flash

See [`CPR_AED_ESP_NOW/ARDUINO_BOARD_SETTINGS.md`](CPR_AED_ESP_NOW/ARDUINO_BOARD_SETTINGS.md)

**Board:** ESP32C3 Dev Module | **Port:** COM8 | **Baud:** 921600

## Repository Structure

```
CPR_Zephyr_AED_Controller/
├── CPR_AED_ESP_NOW/              ← Arduino sketch (ACTIVE FIRMWARE)
│   ├── CPR_AED_ESP_NOW.ino       ← Main sketch (ESP-NOW, no BLE)
│   └── ARDUINO_BOARD_SETTINGS.md
├── src/                          ← Legacy Zephyr source (reference only)
│   ├── aed_config.h
│   ├── aed_button.c / .h
│   ├── aed_led.c / .h
│   ├── aed_ble.c / .h            ← DEPRECATED (not used)
│   └── main.c                    ← DEPRECATED (Zephyr, not used)
├── tools/
│   ├── get_mac_address/          ← Arduino sketch to read ESP32 MAC
│   └── pcb_hardware_test/        ← PCB GPIO test sketch
└── README.md
```

## Related Firmware

- **Manikin MCU:** [`CPR_BLE_Prototype_c3`](../CPR_BLE_Prototype_c3/) — Zephyr/BLE + ESP-NOW receiver
- **GitHub:** [SMARTSsprb/CPR_BLE_Zephyr_Controller_No_LED](https://github.com/SMARTSsprb/CPR_BLE_Zephyr_Controller_No_LED)
