/**
 * @file    aed_led.h
 * @brief   AED Status Indicator LED Driver (Zephyr RTOS)
 * @version 2.1.0
 *
 * Visual Feedback Modes, Color Codes & Waveforms (ESP32-C3):
 *   MODE 01: WAITING FOR PEER  - Deep Blue breathing (1.8s breath cycle, 5% - 100%)
 *   MODE 02: PEER ACTIVE&READY - Emerald Green continuous steady glow (100% Constant Solid ON)
 *   MODE 03: SHOCK ON (ALERT)  - Warm Amber/Yellow one-time triple strobe burst (3x 60ms ON / 60ms OFF)
 *   MODE 04: SHOCK GIVING(ARM) - Urgent Red rapid hazard strobe (5 Hz / 100ms ON / 100ms OFF)
 *   MODE 05: SHOCK OFF (DELIV) - Pure Green 1.0s confirmation latch -> returns to Peer Active Glow
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "aed_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AED_LED_MODE_OFF = 0,
    AED_LED_MODE_WAITING_FOR_PEER,  /* Mode 01: Deep Blue breathing rhythm (1.8s cycle, 5% - 100%) */
    AED_LED_MODE_PEER_ACTIVE,       /* Mode 02: Emerald Green continuous steady glow */
    AED_LED_MODE_SHOCK_ON_ALERT,    /* Mode 03: Warm Amber/Yellow triple strobe burst (3x 60ms/60ms) */
    AED_LED_MODE_SHOCK_GIVING,      /* Mode 04: Urgent Red rapid hazard strobe (5 Hz: 100ms/100ms) */
    AED_LED_MODE_SHOCK_OFF_LATCH    /* Mode 05: Pure Green 1.0s confirmation latch -> Peer Active */
} aed_led_mode_t;

/* Backwards compatibility aliases */
#define AED_LED_MODE_WAITING_FOR_CONN   AED_LED_MODE_WAITING_FOR_PEER
#define AED_LED_MODE_CONNECTED          AED_LED_MODE_PEER_ACTIVE
#define AED_LED_MODE_SHOCK_INSTRUCTION  AED_LED_MODE_SHOCK_ON_ALERT
#define AED_LED_MODE_SHOCK_OFF_PULSE    AED_LED_MODE_SHOCK_OFF_LATCH
/* Solid yellow: AED power is on / shock button ready (also used by shock stage 1) */
#define AED_LED_MODE_POWER_READY        AED_LED_MODE_SHOCK_ON_ALERT

int aed_led_init(void);

void aed_led_set_mode(aed_led_mode_t mode);

aed_led_mode_t aed_led_get_mode(void);

/* Triggers matching the clinical resuscitation visual feedback system */
void aed_led_trigger_waiting_for_peer(void);
void aed_led_trigger_peer_active(void);
void aed_led_trigger_shock_on_alert(void);
void aed_led_trigger_shock_giving(void);
void aed_led_trigger_shock_off(void);
void aed_led_trigger_shock_flash(void);

#ifdef __cplusplus
}
#endif
