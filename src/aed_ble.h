/**
 * @file    aed_ble.h
 * @brief   AED BLE Peripheral Subsystem — Public API (Zephyr RTOS)
 * @version 3.0.0
 *
 * AED role: BLE PERIPHERAL / ADVERTISER ONLY
 *   - Advertises "AED_001" with manufacturer state data
 *   - Accepts connections ONLY from Manikin (Central)
 *   - Application (mobile) reads advertisements only — never connects to AED
 *   - Receives SHOCK_ON / SHOCK_OFF commands from connected Manikin via NUS RX
 *   - On connect  → callback notifies main.c → LED Mode 02 (Solid Green)
 *   - On disconnect → callback notifies main.c → LED Mode 01 (Blue Breathing)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Callback types ─────────────────────────────────────────────────────── */

/** Called when Manikin connects (connected=true) or disconnects (connected=false) */
typedef void (*aed_ble_conn_cb_t)(bool connected);

/** Called when Manikin writes SHOCK_ON (arm=true) or SHOCK_OFF (arm=false) */
typedef void (*aed_ble_shock_cmd_cb_t)(bool arm);

/** Called when AED power mode changes via BLE command (on=true / on=false) */
typedef void (*aed_ble_aed_mode_cb_t)(bool on);

/* ─── Public API ─────────────────────────────────────────────────────────── */

/**
 * @brief  Initialize BLE stack, register GATT service, start advertising.
 *         AED advertises as "AED_001". Manikin connects to send shock commands.
 *         Application only reads advertisement packets — never connects.
 *
 * @param  shock_cb     Called when Manikin sends SHOCK_ON / SHOCK_OFF
 * @param  conn_cb      Called on Manikin connect / disconnect
 * @param  aed_mode_cb  Called on AED_ON / AED_OFF command from Manikin
 * @return 0 on success, negative errno on failure
 */
int aed_ble_init(aed_ble_shock_cmd_cb_t shock_cb,
                 aed_ble_conn_cb_t      conn_cb,
                 aed_ble_aed_mode_cb_t  aed_mode_cb);

/**
 * @brief  Returns true when a Manikin is currently connected.
 */
bool aed_ble_is_connected(void);

/**
 * @brief  Update the manufacturer data state byte in the advertisement payload.
 *         Call this whenever AED internal state changes so the App can see it.
 * @param  state  AED_BLE_STATE_AVAILABLE / AED_BLE_STATE_PAIRED / AED_BLE_STATE_SHOCK
 */
void aed_ble_update_adv_state(uint8_t state);

#ifdef __cplusplus
}
#endif
