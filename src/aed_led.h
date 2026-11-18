/**
 * @file    aed_led.h
 * @brief   AED Status Indicator LED Driver (Zephyr RTOS)
 * @version 1.0.0
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
    AED_LED_MODE_WAITING_FOR_CONN,  /* 1. Deep Blue breathing rhythm (3.0s organic cycle) */
    AED_LED_MODE_CONNECTED,         /* 2. Solid Green (BLE connected & ready) */
    AED_LED_MODE_SHOCK_INSTRUCTION, /* 3. Yellow / Amber for 5.0s (Voice instruction / charging) */
    AED_LED_MODE_SHOCK_GIVING,      /* 4. Urgent Red for 5.0s (Shock giving phase) */
    AED_LED_MODE_SHOCK_OFF_PULSE    /* 5. Return to Solid Green */
} aed_led_mode_t;

/* Backwards compatibility aliases */
#define AED_LED_MODE_SHOCK_ON_ALERT AED_LED_MODE_SHOCK_INSTRUCTION

int aed_led_init(void);

void aed_led_set_mode(aed_led_mode_t mode);

aed_led_mode_t aed_led_get_mode(void);

/* One-time Amber alert when shock mode is enabled (SHOCK ON) */
void aed_led_trigger_shock_on_alert(void);

/* Urgent 5 Hz Red hazard strobe while shock is being given */
void aed_led_trigger_shock_giving(void);

/* 1.0s Solid Green confirmation when shock is delivered / turned off */
void aed_led_trigger_shock_off(void);

/* Compatibility alias for existing codebase */
void aed_led_trigger_shock_flash(void);

#ifdef __cplusplus
}
#endif

