/**
 * @file    aed_config.h
 * @brief   System & Hardware Configuration for External AED Shock Unit (Zephyr RTOS)
 * @version 3.0.0
 * @note    Protocol: BLE PERIPHERAL
 *          AED advertises as "AED_001". The mobile App (central) connects directly,
 *          pairs with a passkey and controls the AED with short commands
 *          (C ?  P0 P1  S0 S1). The AED notifies power / shock events to the App.
 *          The Manikin is a separate device, also connected to the App.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define AED_FW_VERSION_STR       "3.0.0-ZEPHYR-BLE"
#define AED_PROJECT_NAME         "CPR_AED_BLE"

/* =============================================================================
 * BLE CONFIGURATION
 * -----------------------------------------------------------------------------
 * AED advertises under this name. App scans and assigns one AED to one Manikin.
 * Manikin then connects to this AED as Central. App never connects to AED.
 * ============================================================================= */
#define AED_BLE_DEVICE_NAME      "AED_001"          /* Matches CONFIG_BT_DEVICE_NAME in prj.conf */
#define AED_BLE_MFG_COMPANY_ID   0xC9C4             /* SMARTS manufacturer ID (little-endian) */

/* Manufacturer data state codes embedded in advertisement */
#define AED_BLE_STATE_AVAILABLE  0x00   /* Idle - waiting for the App */
#define AED_BLE_STATE_CONNECTED  0x01   /* App connected */

/* Fixed 6-digit pairing passkey (the App user types it when pairing).
 * CHANGE PER UNIT before shipping and print it on the unit label. */
#define AED_BLE_PASSKEY          123456

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
#define TIMING_SYNC_PULSE_MS         1     /* Hardware sync pulse duration (1 ms) */

/* MODE 01: WAITING FOR PEER / STANDBY (Deep Blue Breathing: 3.0s organic cycle, 150 steps @ 20ms) */
#define TIMING_LED_BREATH_TICK_MS    20    /* PWM/Breathing engine tick interval (20ms) */
#define TIMING_LED_BREATH_STEPS      150   /* 150 steps * 20ms = 3000ms (3.0s organic breath cycle) */

/* SHOCK SEQUENCE TIMINGS:
 * Stage 1: Yellow / Amber (Application instruction / charging) for 5.0 seconds
 * Stage 2: Red (Active shock delivery + 1ms sync pulse) for 5.0 seconds
 * Stage 3: Returns to Solid Green (Safe / shock complete)
 */
#define TIMING_SHOCK_INSTRUCTION_MS  5000  /* 5.0 seconds Yellow (Instruction giving from application) */
#define TIMING_SHOCK_GIVING_MS       5000  /* 5.0 seconds Red (Shock giving) */
#define TIMING_SHOCK_OFF_LATCH_MS    1000  /* 1.0s solid green latch */
#define TIMING_SHOCK_GIVING_STROBE_MS 100  /* 100ms ON / 100ms OFF (5 Hz urgent strobe) */

/* =============================================================================
 * AED STATE ENUMS & CODES
 * ============================================================================= */
typedef enum {
    AED_STATE_DISARMED = 0,   /* Standby / Peer Active (Mode 02) */
    AED_STATE_ARMED    = 1,   /* Armed / Alert Stage (Mode 03) */
    AED_STATE_SHOCK    = 2    /* Delivering Shock (Mode 04) */
} aed_state_t;
