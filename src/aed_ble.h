/**
 * @file    aed_ble.h
 * @brief   AED BLE Peripheral Subsystem - Public API (Zephyr RTOS)
 * @version 4.0.0
 *
 * AED role: BLE PERIPHERAL. The mobile App is the BLE central and connects
 * directly to the AED (the Manikin is a separate device, also connected to
 * the App; AED and Manikin never talk to each other).
 *
 *   App -> AED commands (NUS RX write, plain ASCII):
 *     C or ?   announce request
 *     P0 / P1  power button disable / enable
 *     S0 / S1  shock button disable / enable
 *
 *   AED -> App events (NUS TX notify): see aed_protocol.h
 *
 * Link security: LE Secure Connections only, authenticated (passkey),
 * bonded. Every GATT attribute requires an authenticated, encrypted link.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AED_CMD_ANNOUNCE = 1,   /* 'C' or '?' */
    AED_CMD_P0,             /* power button disable */
    AED_CMD_P1,             /* power button enable  */
    AED_CMD_S0,             /* shock button disable */
    AED_CMD_S1,             /* shock button enable  */
} aed_cmd_t;

/** Called (BLE thread) for every valid command written by the App */
typedef void (*aed_ble_cmd_cb_t)(aed_cmd_t cmd);

/** Called (BLE thread) when the App connects (true) or disconnects (false) */
typedef void (*aed_ble_link_cb_t)(bool connected);

/** Called (BLE thread) when the App enables NUS TX notifications */
typedef void (*aed_ble_notify_cb_t)(void);

/**
 * @brief  Initialize BLE stack, security, GATT service and start advertising.
 * @return 0 on success, negative errno on failure
 */
int aed_ble_init(aed_ble_cmd_cb_t cmd_cb,
                 aed_ble_link_cb_t link_cb,
                 aed_ble_notify_cb_t notify_cb);

/** True while the App is connected */
bool aed_ble_is_connected(void);

/**
 * @brief  Notify one event to the App (15 byte packet).
 * @param  type   PTYPE_AED_POWER or PTYPE_AED_SHOCK
 * @param  value  event value
 * @return 0 on success, negative errno if App not connected / not subscribed
 */
int aed_ble_send_event(uint8_t type, int8_t value);

/** Notify the announce packet (reply to 'C' / '?') */
int aed_ble_send_announce(void);

#ifdef __cplusplus
}
#endif
