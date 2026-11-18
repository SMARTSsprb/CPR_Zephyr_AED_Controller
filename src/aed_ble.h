/**
 * @file    aed_ble.h
 * @brief   AED BLE Subsystem & Wireless Telemetry (Zephyr RTOS)
 * @version 3.0.0
 * 
 * STRICT 4-COMMAND PROTOCOL SPECIFICATION:
 *  1. AED ON          : "AED_ON"   / 'A1'
 *  2. AED OFF         : "AED_OFF"  / 'A0'
 *  3. Shock ON (Arm)  : "SHOCK_ON" / 'K1' / 'E'
 *  4. Shock OFF (Disarm): "SHOCK_OFF" / 'K0' / 'D'
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "aed_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*aed_ble_shock_cmd_cb_t)(bool armed);
typedef void (*aed_ble_conn_cb_t)(bool connected);
typedef void (*aed_ble_aed_mode_cb_t)(bool active);

/* Initialize BLE subsystem with strict callbacks */
int aed_ble_init(aed_ble_shock_cmd_cb_t shock_cb,
                 aed_ble_conn_cb_t conn_cb,
                 aed_ble_aed_mode_cb_t aed_mode_cb);

bool aed_ble_is_connected(void);

/* =============================================================================
 * STRICT TRANSMISSION API (ONLY THESE 4 PACKETS ARE SENT)
 * ============================================================================= */
int aed_ble_send_aed_on(void);     /* Sends "AED_ON\n" */
int aed_ble_send_aed_off(void);    /* Sends "AED_OFF\n" */
int aed_ble_send_shock_on(void);   /* Sends "SHOCK_ON\n" */
int aed_ble_send_shock_off(void);  /* Sends "SHOCK_OFF\n" */

#ifdef __cplusplus
}
#endif
