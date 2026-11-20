/**
 * @file    aed_ble.c
 * @brief   AED BLE Peripheral Subsystem Implementation (Zephyr RTOS)
 * @version 3.0.0
 *
 * ROLE: BLE PERIPHERAL / ADVERTISER
 * ─────────────────────────────────────────────────────────────────────────────
 * Flow:
 *   1. AED boots → starts advertising as "AED_001" (connectable)
 *   2. App scans, discovers "AED_001" in advertisement list
 *   3. App assigns this AED to a Manikin (App ↔ Manikin link, not App ↔ AED)
 *   4. Manikin (Central) connects to AED
 *   5. On connect  → LED Mode 02 (Solid Green) via conn_cb
 *   6. Manikin writes SHOCK_ON / SHOCK_OFF to NUS RX → shock_cb fires
 *   7. On disconnect → LED Mode 01 (Blue Breathing) → restart advertising
 *
 * STRICT 4-COMMAND RECEIVE PROTOCOL:
 *   SHOCK_ON  / 'E' / 'K1' — trigger shock sequence
 *   SHOCK_OFF / 'D' / 'K0' — (optional disarm)
 *   AED_ON    / 'A1'        — power on notification from Manikin
 *   AED_OFF   / 'A0'        — power off notification from Manikin
 *
 * NO DATA IS SENT TO THE APPLICATION. Application only reads advertisement packets.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "aed_ble.h"
#include "aed_config.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>

LOG_MODULE_REGISTER(aed_ble, LOG_LEVEL_INF);

/* ─── NUS UUIDs ──────────────────────────────────────────────────────────── */
#define BT_UUID_NUS_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x6e400001, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
#define BT_UUID_NUS_RX_VAL \
    BT_UUID_128_ENCODE(0x6e400002, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
#define BT_UUID_NUS_TX_VAL \
    BT_UUID_128_ENCODE(0x6e400003, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)

static struct bt_uuid_128 nus_uuid    = BT_UUID_INIT_128(BT_UUID_NUS_SERVICE_VAL);
static struct bt_uuid_128 nus_rx_uuid = BT_UUID_INIT_128(BT_UUID_NUS_RX_VAL);
static struct bt_uuid_128 nus_tx_uuid = BT_UUID_INIT_128(BT_UUID_NUS_TX_VAL);

/* ─── State ──────────────────────────────────────────────────────────────── */
static struct bt_conn *s_conn           = NULL;
static volatile bool   s_connected     = false;
static volatile bool   s_advertising   = false;
static bool            s_notify_en     = false;

static aed_ble_shock_cmd_cb_t  s_shock_cb    = NULL;
static aed_ble_conn_cb_t       s_conn_cb     = NULL;
static aed_ble_aed_mode_cb_t   s_aed_mode_cb = NULL;

static struct k_work_delayable s_adv_restart_work;

/* ─── Advertisement payload ─────────────────────────────────────────────── *
 * Manufacturer Data layout:
 *   Byte 0-1: Company ID low byte, high byte  (0xC4, 0xC9 → LE for 0xC9C4)
 *   Byte 2  : AED state  (0x00=available, 0x01=paired, 0x02=shock)
 */
static uint8_t s_mfg_data[3] = {
    (uint8_t)(AED_BLE_MFG_COMPANY_ID & 0xFF),       /* 0xC4 */
    (uint8_t)((AED_BLE_MFG_COMPANY_ID >> 8) & 0xFF),/* 0xC9 */
    AED_BLE_STATE_AVAILABLE
};

static const struct bt_data s_ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE,
            AED_BLE_DEVICE_NAME, sizeof(AED_BLE_DEVICE_NAME) - 1),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, s_mfg_data, sizeof(s_mfg_data)),
};

/* Scan response: expose NUS service UUID so Manikin can filter */
static const struct bt_data s_sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SERVICE_VAL),
};

/* ─── GATT: NUS TX (notify only — AED → Manikin, reserved) ──────────────── */
static uint8_t  s_tx_buf[16];
static uint16_t s_tx_len = 0;

static ssize_t on_nus_tx_read(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, s_tx_buf, s_tx_len);
}

static void on_nus_tx_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    s_notify_en = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("[AED BLE] NUS TX notify %s", s_notify_en ? "enabled" : "disabled");
}

/* ─── GATT: NUS RX (write — Manikin → AED) ─────────────────────────────── */
static ssize_t on_nus_rx_write(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len,
                                uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    if (len < 1) return len;
    const char *cmd = (const char *)buf;

    /* ── SHOCK ON: 'E', 'K1', "SHOCK_ON" ─────────────────────────── */
    if (cmd[0] == 'E' ||
        (len >= 2 && cmd[0] == 'K' && cmd[1] == '1') ||
        (len >= 8 && memcmp(cmd, "SHOCK_ON", 8) == 0)) {
        LOG_INF("[RX] SHOCK_ON received from Manikin");
        if (s_shock_cb) s_shock_cb(true);
        return len;
    }

    /* ── SHOCK OFF: 'D', 'K0', "SHOCK_OFF" ───────────────────────── */
    if (cmd[0] == 'D' ||
        (len >= 2 && cmd[0] == 'K' && cmd[1] == '0') ||
        (len >= 9 && memcmp(cmd, "SHOCK_OFF", 9) == 0)) {
        LOG_INF("[RX] SHOCK_OFF received from Manikin");
        if (s_shock_cb) s_shock_cb(false);
        return len;
    }

    /* ── AED ON: 'A1', "AED_ON" ───────────────────────────────────── */
    if ((len >= 2 && cmd[0] == 'A' && cmd[1] == '1') ||
        (len >= 6 && memcmp(cmd, "AED_ON", 6) == 0)) {
        LOG_INF("[RX] AED_ON received from Manikin");
        if (s_aed_mode_cb) s_aed_mode_cb(true);
        return len;
    }

    /* ── AED OFF: 'A0', "AED_OFF" ─────────────────────────────────── */
    if ((len >= 2 && cmd[0] == 'A' && cmd[1] == '0') ||
        (len >= 7 && memcmp(cmd, "AED_OFF", 7) == 0)) {
        LOG_INF("[RX] AED_OFF received from Manikin");
        if (s_aed_mode_cb) s_aed_mode_cb(false);
        return len;
    }

    LOG_WRN("[RX BLOCKED] Unknown packet from Manikin: '%.*s'", len, cmd);
    return len;
}

/* ─── GATT Service ──────────────────────────────────────────────────────── */
BT_GATT_SERVICE_DEFINE(aed_nus_svc,
    BT_GATT_PRIMARY_SERVICE(&nus_uuid),

    /* NUS RX — Manikin writes shock commands here */
    BT_GATT_CHARACTERISTIC(&nus_rx_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_nus_rx_write, NULL),

    /* NUS TX — Reserved for future AED→Manikin notifications */
    BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           on_nus_tx_read, NULL, NULL),
    BT_GATT_CCC(on_nus_tx_ccc_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* ─── Advertising ───────────────────────────────────────────────────────── */
static int start_advertising(void)
{
    if (s_connected) return 0;

    bt_le_adv_stop();
    s_advertising = false;

    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                               s_ad, ARRAY_SIZE(s_ad),
                               s_sd, ARRAY_SIZE(s_sd));
    if (err) {
        LOG_ERR("[AED BLE] bt_le_adv_start failed (err %d)", err);
        return err;
    }

    s_advertising = true;
    LOG_INF("[AED BLE] Advertising as '%s' — waiting for Manikin...", AED_BLE_DEVICE_NAME);
    return 0;
}

static void adv_restart_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    if (!s_connected) {
        int err = start_advertising();
        if (err) {
            /* retry in 500ms */
            k_work_reschedule(&s_adv_restart_work, K_MSEC(500));
        }
    }
}

/* ─── Connection Callbacks ──────────────────────────────────────────────── */
static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("[AED BLE] Connection failed (err 0x%02x) — restarting adv", err);
        k_work_reschedule(&s_adv_restart_work, K_MSEC(100));
        return;
    }

    k_work_cancel_delayable(&s_adv_restart_work);
    s_conn      = bt_conn_ref(conn);
    s_connected = true;
    s_advertising = false;

    LOG_INF("══════════════════════════════════════════════════");
    LOG_INF(">>> [AED BLE] MANIKIN CONNECTED — Mode 02 Active <<<");
    LOG_INF("══════════════════════════════════════════════════");

    s_mfg_data[2] = AED_BLE_STATE_PAIRED;
    if (s_conn_cb) s_conn_cb(true);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("<<< [AED BLE] MANIKIN DISCONNECTED (reason 0x%02x) — restarting adv >>>", reason);

    if (s_conn) {
        bt_conn_unref(s_conn);
        s_conn = NULL;
    }
    s_connected   = false;
    s_advertising = false;
    s_notify_en   = false;

    s_mfg_data[2] = AED_BLE_STATE_AVAILABLE;
    if (s_conn_cb) s_conn_cb(false);

    /* Short delay then resume advertising */
    k_work_reschedule(&s_adv_restart_work, K_MSEC(100));
}

static void on_recycled(void)
{
    LOG_INF("[AED BLE] Connection object recycled — resuming advertising");
    k_work_reschedule(&s_adv_restart_work, K_NO_WAIT);
}

BT_CONN_CB_DEFINE(aed_conn_callbacks) = {
    .connected    = on_connected,
    .disconnected = on_disconnected,
    .recycled     = on_recycled,
};

/* ─── Public API ────────────────────────────────────────────────────────── */

int aed_ble_init(aed_ble_shock_cmd_cb_t shock_cb,
                 aed_ble_conn_cb_t      conn_cb,
                 aed_ble_aed_mode_cb_t  aed_mode_cb)
{
    s_shock_cb    = shock_cb;
    s_conn_cb     = conn_cb;
    s_aed_mode_cb = aed_mode_cb;

    k_work_init_delayable(&s_adv_restart_work, adv_restart_work_handler);

    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("[AED BLE] bt_enable failed (err %d)", err);
        return err;
    }

    LOG_INF("[AED BLE] BLE stack enabled. Starting advertising as '%s'...",
            AED_BLE_DEVICE_NAME);
    return start_advertising();
}

bool aed_ble_is_connected(void)
{
    return s_connected;
}

void aed_ble_update_adv_state(uint8_t state)
{
    s_mfg_data[2] = state;
    /* If currently advertising, restart to push updated manufacturer data */
    if (s_advertising && !s_connected) {
        bt_le_adv_stop();
        s_advertising = false;
        int err = start_advertising();
        if (err) {
            LOG_WRN("[AED BLE] adv state update re-start failed (err %d)", err);
        }
    }
}
