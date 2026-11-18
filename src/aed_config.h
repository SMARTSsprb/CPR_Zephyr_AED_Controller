/**
 * @file    aed_config.h
 * @brief   System & Hardware Configuration for External AED Shock Unit (Zephyr RTOS)
 * @version 2.1.0
 * @note    Protocol: ESP-NOW ONLY (No BLE)
 *          Visual Feedback Modes, Color Codes & Waveforms match clinical resuscitation rationale.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define AED_FW_VERSION_STR       "2.1.0-ZEPHYR-ESPNOW"
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
 * Red: GPIO 3 | Green: GPIO 4 | Blue: GPIO 5 | Sync: GPIO 7 | Shock: GPIO 9
 * ============================================================================= */
#define HAL_PIN_SHOCK_BUTTON     9   /* Active-LOW Shock Button (Onboard BOOT or External) */
#define HAL_PIN_POWER_BUTTON     6   /* Optional GPIO 6 logic power pin (if not using Option 1) */
#define HAL_PIN_STATUS_LED       8   /* Active-LOW Status LED (Onboard Blue LED) */
#define HAL_PIN_SYNC_OUT         7   /* Hardware Trigger Pulse Line to Manikin (1ms sync) */

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
#define TIMING_SHOCK_COOLDOWN_MS     11000 /* 11s safety lockout (entire sequence + guard) */
#define TIMING_SYNC_PULSE_MS         1     /* Hardware sync pulse duration (1 ms) */

/* MODE 01: WAITING FOR PEER (Deep Blue Breathing: 3.0s organic cycle, 150 steps @ 20ms) */
#define TIMING_LED_BREATH_TICK_MS    20    /* PWM/Breathing engine tick interval (20ms) */
#define TIMING_LED_BREATH_STEPS      150   /* 150 steps * 20ms = 3000ms (3.0s organic breath cycle) */

/* MODE 03: SHOCK ON ALERT (Warm Amber/Yellow Triple Strobe Burst) */
#define TIMING_SHOCK_ALERT_STROBE_MS 60    /* 60ms ON / 60ms OFF */
#define SHOCK_ALERT_STROBE_CYCLES    3     /* 3 bursts = ~360ms total */
#define SHOCK_ALERT_TOTAL_BURST_MS   360   /* 60ms * 6 states = 360ms */

/* MODE 04: SHOCK GIVING ARMED (Urgent Red Hazard Strobe: 5 Hz Rhythm) */
#define TIMING_SHOCK_GIVING_STROBE_MS 100  /* 100ms ON / 100ms OFF (5 Hz / 300 bpm) */
#define TIMING_SHOCK_GIVING_MS       5000  /* 5.0 seconds active shock delivery */

/* MODE 05: SHOCK OFF DELIVERED (Pure Green 1.0s Confirmation Latch) */
#define TIMING_SHOCK_OFF_LATCH_MS    1000  /* 1000ms Solid Green pulse before returning to Mode 02 */

/* =============================================================================
 * AED STATE ENUMS & CODES
 * ============================================================================= */
typedef enum {
    AED_STATE_DISARMED = 0,   /* Standby / Peer Active (Mode 02) */
    AED_STATE_ARMED    = 1,   /* Armed / Alert Stage (Mode 03) */
    AED_STATE_SHOCK    = 2    /* Delivering Shock (Mode 04) */
} aed_state_t;
