/**
 * @file    aed_espnow.c
 * @brief   AED ESP-NOW Wireless Subsystem (Zephyr RTOS + Espressif HAL)
 * @version 1.0.0
 *
 * Uses the Espressif ESP-IDF HAL directly (esp_now.h, esp_wifi.h) which are
 * available under Zephyr via the espressif HAL module.
 *
 * NOTE: Zephyr's CONFIG_WIFI_ESP32 enables the Wi-Fi driver in STA mode
 *       which is required to power the RF hardware before esp_now_init().
 */

#include "aed_espnow.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Espressif HAL — available via zephyr/modules/hal/espressif */
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_err.h>
#include <esp_mac.h>

LOG_MODULE_REGISTER(aed_espnow, LOG_LEVEL_INF);

/* ─── Internal state ───────────────────────────────────────────────────────── */
static bool                  s_ready    = false;
static aed_espnow_sent_cb_t  s_sent_cb  = NULL;

static const uint8_t s_manikin_mac[6] = ESPNOW_MANIKIN_RX_MAC;

/* ─── ESP-NOW send callback (called from Wi-Fi task context) ──────────────── */
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    bool ok = (status == ESP_NOW_SEND_SUCCESS);

    if (tx_info && tx_info->des_addr) {
        LOG_INF("[ESP-NOW] TX -> %02X:%02X:%02X:%02X:%02X:%02X : %s",
                tx_info->des_addr[0], tx_info->des_addr[1], tx_info->des_addr[2],
                tx_info->des_addr[3], tx_info->des_addr[4], tx_info->des_addr[5],
                ok ? "SUCCESS" : "FAILED");
    } else {
        LOG_INF("[ESP-NOW] TX : %s", ok ? "SUCCESS" : "FAILED");
    }

    if (s_sent_cb) {
        s_sent_cb(ok);
    }
}

/* ─── Internal transmit helper ────────────────────────────────────────────── */
static int espnow_tx(uint8_t cmd, uint8_t stage)
{
    if (!s_ready) {
        LOG_WRN("[ESP-NOW] Not initialized — packet dropped (cmd=%d stage=%d)", cmd, stage);
        return -ENODEV;
    }

    aed_espnow_pkt_t pkt = {
        .cmd    = cmd,
        .stage  = stage,
        .ts_ms  = k_uptime_get_32()
    };

    esp_err_t err = esp_now_send(s_manikin_mac, (const uint8_t *)&pkt, sizeof(pkt));
    if (err != ESP_OK) {
        LOG_ERR("[ESP-NOW] esp_now_send failed (err=0x%x cmd=%d stage=%d)", err, cmd, stage);
        return -EIO;
    }

    LOG_INF("[ESP-NOW] TX queued cmd=%d stage=%d ts=%u ms", cmd, stage, pkt.ts_ms);
    return 0;
}

/* ─── Public API ──────────────────────────────────────────────────────────── */

int aed_espnow_init(aed_espnow_sent_cb_t sent_cb)
{
    s_sent_cb = sent_cb;

    /* 1. Print this board's Wi-Fi STA MAC for reference */
    uint8_t my_mac[6] = {0};
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    LOG_INF("══════════════════════════════════════════════════");
    LOG_INF(" [AED MCU ADDRESS]");
    LOG_INF(" AED  Wi-Fi STA MAC : %02X:%02X:%02X:%02X:%02X:%02X",
            my_mac[0], my_mac[1], my_mac[2],
            my_mac[3], my_mac[4], my_mac[5]);
    LOG_INF(" Target Manikin MAC : %02X:%02X:%02X:%02X:%02X:%02X",
            s_manikin_mac[0], s_manikin_mac[1], s_manikin_mac[2],
            s_manikin_mac[3], s_manikin_mac[4], s_manikin_mac[5]);
    LOG_INF(" ESP-NOW Channel    : %d", ESPNOW_WIFI_CHANNEL);
    LOG_INF("══════════════════════════════════════════════════");

    /* 2. Set Wi-Fi to STA mode (required for ESP-NOW) */
    wifi_config_t wifi_config = {0};
    esp_err_t err = esp_wifi_set_mode(ESP32_WIFI_MODE_STA);
    if (err != ESP_OK) {
        LOG_ERR("esp_wifi_set_mode(STA) failed: 0x%x", err);
        return -EIO;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        LOG_WRN("esp_wifi_set_config returned 0x%x (non-fatal)", err);
    }

    /* 3. Initialize ESP-NOW */
    err = esp_now_init();
    if (err != ESP_OK) {
        LOG_ERR("esp_now_init() failed: 0x%x", err);
        return -EIO;
    }

    /* 4. Register send status callback */
    err = esp_now_register_send_cb(espnow_send_cb);
    if (err != ESP_OK) {
        LOG_WRN("esp_now_register_send_cb failed: 0x%x (non-fatal)", err);
    }

    /* 5. Add Manikin as ESP-NOW peer */
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_manikin_mac, 6);
    peer.channel = ESPNOW_WIFI_CHANNEL;
    peer.encrypt = false;
    peer.ifidx   = WIFI_IF_STA;

    err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        LOG_ERR("esp_now_add_peer() failed: 0x%x", err);
        esp_now_deinit();
        return -EIO;
    }

    s_ready = true;
    LOG_INF("[ESP-NOW] Initialized. Manikin peer registered. Ready to send.");
    return 0;
}

bool aed_espnow_is_ready(void)
{
    return s_ready;
}

int aed_espnow_send_aed_on(void)
{
    LOG_INF(">>> Sending AED_ON (cmd=1, stage=0)");
    return espnow_tx(ESPNOW_CMD_AED_ON, 0);
}

int aed_espnow_send_aed_off(void)
{
    LOG_INF(">>> Sending AED_OFF (cmd=0, stage=0)");
    return espnow_tx(ESPNOW_CMD_AED_OFF, 0);
}

int aed_espnow_send_shock_on(uint8_t stage)
{
    LOG_INF(">>> Sending SHOCK_ON (cmd=2, stage=%d)", stage);
    return espnow_tx(ESPNOW_CMD_SHOCK_ON, stage);
}

int aed_espnow_send_shock_off(void)
{
    LOG_INF(">>> Sending SHOCK_OFF (cmd=3, stage=3)");
    return espnow_tx(ESPNOW_CMD_SHOCK_OFF, 3);
}
