/**
 * @file    aed_config.h
 * @brief   System & Hardware Configuration for External AED Shock Unit (Zephyr RTOS)
 * @version 2.0.0
 * @note    Protocol: ESP-NOW ONLY (No BLE)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define AED_FW_VERSION_STR       "2.0.0-ZEPHYR-ESPNOW"
#define AED_PROJECT_NAME         "CPR_AED_ESPNOW"

/* =============================================================================
 * ESP-NOW TARGET PEER CONFIGURATION (Receiver / Manikin MCU Address)
 * -----------------------------------------------------------------------------
 * Target Manikin MCU MAC Addresses:
 *   - Wi-Fi STA MAC (ESP-NOW): E8:3D:C1:82:1B:F0 -> { 0xE8, 0x3D, 0xC1, 0x82, 0x1B, 0xF0 }
 *   - Bluetooth LE MAC:        E8:3D:C1:82:1B:F2
 * ============================================================================= */
#define ESPNOW_MANIKIN_RX_MAC    { 0xE8, 0x3D, 0xC1, 0x82, 0x1B, 0xF0 }  /* Manikin MCU Wi-Fi STA MAC */
#define ESPNOW_WIFI_CHANNEL      1                                       /* 2.4 GHz Wi-Fi Channel (1 - 13) */

/* =============================================================================
 * HARDWARE GPIO ASSIGNMENTS (ESP32-C3 Super Mini)
 * ============================================================================= */
#define HAL_PIN_SHOCK_BUTTON     9   /* Active-LOW Shock Button (Onboard BOOT or External) */
#define HAL_PIN_POWER_BUTTON     6   /* Optional GPIO 6 logic power pin (if not using Option 1) */
#define HAL_PIN_STATUS_LED       8   /* Active-LOW Status LED (Onboard Blue LED) */
#define HAL_PIN_SYNC_OUT         7   /* Hardware Trigger Pulse Line to Manikin (Optional) */

/* 
 * Power Switch Architecture:
 * 1 = Option 1: In-Line Battery Latching Switch (Hardware power cut). [ACTIVE]
 *               Switch physically connects/disconnects Battery (+) to ESP32-C3 5V/VIN.
 *               Zero standby current drain (0 uA). Board is active when powered.
 * 0 = Option 2: Logic Switch on GPIO 6 (Soft power / deep sleep).
 */
#define AED_POWER_SWITCH_HARDWARE_CUT 1

/* RGB LED Pins (3-Channel discrete or Common Cathode/Anode module) */
#define HAL_PIN_RGB_RED          3   /* RGB Red Channel (GPIO 3) */
#define HAL_PIN_RGB_GREEN        4   /* RGB Green Channel (GPIO 4) */
#define HAL_PIN_RGB_BLUE         5   /* RGB Blue Channel (GPIO 5) */

/* 
 * RGB LED Electrical Polarity:
 * 0 = Common Cathode (Active-HIGH: Logic 1 = ON, Logic 0 = OFF) [DEFAULT]
 * 1 = Common Anode   (Active-LOW:  Logic 0 = ON, Logic 1 = OFF)
 */
#define RGB_LED_ACTIVE_LOW       0

/* =============================================================================
 * TIMING & OPERATIONAL CONSTANTS (Milliseconds)
 * ============================================================================= */
#define TIMING_BTN_DEBOUNCE_MS       50    /* Button debounce time */
#define TIMING_SHOCK_COOLDOWN_MS     11000 /* 11s safety lockout (entire 10s sequence + 1s guard) */
#define TIMING_SYNC_PULSE_MS         1     /* Hardware sync pulse duration (1 ms) */
#define TIMING_SHOCK_INSTRUCTION_MS  5000  /* 5.0 seconds Yellow (Application instruction / charging) */
#define TIMING_SHOCK_GIVING_MS       5000  /* 5.0 seconds Red (Active shock delivery) */
#define TIMING_SHOCK_OFF_LATCH_MS    1000  /* 1.0s solid Green latch on shock completion */
#define TIMING_LED_BREATH_TICK_MS    20    /* PWM/Breathing engine tick interval (20ms) */
#define TIMING_LED_BREATH_STEPS      150   /* 150 steps * 20ms = 3000ms (3.0s organic breath cycle) */

/* =============================================================================
 * AED STATE ENUMS & CODES
 * ============================================================================= */
typedef enum {
    AED_STATE_DISARMED = 0,   /* Standby / Disarmed */
    AED_STATE_ARMED    = 1,   /* Armed / Ready for Shock (Blinking Fast) */
    AED_STATE_SHOCK    = 2    /* Shock Button Pressed / Shock Triggered */
} aed_state_t;

/* ESP-NOW Sync Pulse timing (also used by aed_button_fire_sync_pulse) */
/* TIMING_SYNC_PULSE_MS is in milliseconds; pass (ms * 1000) for k_busy_wait (µs) */

