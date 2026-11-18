/**
 * @file    aed_button.h
 * @brief   AED Shock Button Driver with Hardware Debounce & Safety Lockout
 * @version 1.0.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "aed_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*aed_shock_btn_cb_t)(void);
typedef void (*aed_power_btn_cb_t)(bool is_switch_on);

int aed_button_init(aed_shock_btn_cb_t shock_cb, aed_power_btn_cb_t power_cb);

bool aed_button_is_pressed(void);

/* Reads physical latching power switch level (true = ON / GND, false = OFF / High-Z) */
bool aed_power_switch_is_on(void);

/* Manually trigger high sync pulse on GPIO 7 (e.g. 1000 us = 1 ms) */
void aed_button_fire_sync_pulse(uint32_t duration_us);

#ifdef __cplusplus
}
#endif

