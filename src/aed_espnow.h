/**
 * @file    aed_espnow.h
 * @brief   AED ESP-NOW Wireless Subsystem (Zephyr RTOS + Espressif HAL)
 * @version 1.0.0
 *
 * STRICT 4-COMMAND PROTOCOL (AED → Manikin MCU via ESP-NOW):
 *  1. AED_ON    (cmd=1, stage=0) — Sent on boot / power-on
 *  2. AED_OFF   (cmd=0, stage=0) — Sent on power-off
 *  3. SHOCK_ON  (cmd=2, stage=1) — Stage 1: Yellow / Instruction
 *  4. SHOCK_ON  (cmd=2, stage=2) — Stage 2: Red / Delivering
 *  5. SHOCK_OFF (cmd=3, stage=3) — Stage 3: Shock complete
 *
 * Target Manikin Wi-Fi STA MAC: E8:3D:C1:82:1B:F0 (channel 1)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "aed_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── ESP-NOW Packet (packed, 6 bytes) ────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  cmd;    /* 0=AED_OFF, 1=AED_ON, 2=SHOCK_ON, 3=SHOCK_OFF */
    uint8_t  stage;  /* 0=idle, 1=yellow/instruction, 2=red/delivering, 3=done */
    uint32_t ts_ms;  /* k_uptime_get_32() timestamp (milliseconds) */
} aed_espnow_pkt_t;

/* Command codes */
#define ESPNOW_CMD_AED_OFF   0
#define ESPNOW_CMD_AED_ON    1
#define ESPNOW_CMD_SHOCK_ON  2
#define ESPNOW_CMD_SHOCK_OFF 3

/* ─── Send callback (optional — for delivery status logging) ─────────────── */
typedef void (*aed_espnow_sent_cb_t)(bool success);

/* ─── Public API ─────────────────────────────────────────────────────────── */

/**
 * @brief  Initialize Wi-Fi (STA mode) and ESP-NOW, register Manikin as peer.
 * @return 0 on success, negative errno on failure.
 */
int aed_espnow_init(aed_espnow_sent_cb_t sent_cb);

/** @brief Send AED_ON  packet (cmd=1, stage=0) */
int aed_espnow_send_aed_on(void);

/** @brief Send AED_OFF packet (cmd=0, stage=0) */
int aed_espnow_send_aed_off(void);

/**
 * @brief Send SHOCK_ON packet with shock stage.
 * @param stage  1 = instruction/yellow, 2 = delivering/red
 */
int aed_espnow_send_shock_on(uint8_t stage);

/** @brief Send SHOCK_OFF packet (cmd=3, stage=3) */
int aed_espnow_send_shock_off(void);

/** @brief Returns true if ESP-NOW is initialized and peer is registered. */
bool aed_espnow_is_ready(void);

#ifdef __cplusplus
}
#endif
