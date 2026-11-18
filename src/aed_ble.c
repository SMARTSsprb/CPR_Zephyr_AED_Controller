/**
 * @file    aed_ble.c
 * @brief   AED BLE Subsystem & Wireless Telemetry Implementation (Zephyr RTOS)
 * @version 3.0.0
 * 
 * STRICT 4-COMMAND PROTOCOL:
 *  - Transmit: ONLY "AED_ON\n", "AED_OFF\n", "SHOCK_ON\n", "SHOCK_OFF\n"
 *  - Receive : ONLY 'A1' / "AED_ON", 'A0' / "AED_OFF", 'K1' / 'E' / "SHOCK_ON", 'K0' / 'D' / "SHOCK_OFF"
 *  - All other incoming / outgoing packets are strictly rejected.
 */

#include "aed_ble.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

LOG_MODULE_REGISTER(aed_ble, LOG_LEVEL_INF);

static struct bt_conn *s_conn = NULL;
static volatile bool s_connected = false;
static volatile bool s_advertising = false;
static bool s_tx_notify_enabled = false;

static aed_ble_shock_cmd_cb_t s_shock_cb    = NULL;
static aed_ble_conn_cb_t      s_conn_cb     = NULL;
static aed_ble_aed_mode_cb_t  s_aed_mode_cb = NULL;

static struct k_work_delayable s_adv_work;

/* Nordic UART Service (NUS) UUIDs */
static struct bt_uuid_128 nus_uuid    = BT_UUID_INIT_128(BT_UUID_NUS_SERVICE_VAL);
static struct bt_uuid_128 nus_rx_uuid = BT_UUID_INIT_128(BT_UUID_NUS_RX_VAL);
static struct bt_uuid_128 nus_tx_uuid = BT_UUID_INIT_128(BT_UUID_NUS_TX_VAL);

static uint8_t s_tx_buffer[16];
static uint16_t s_tx_len = 0;

/* Manufacturer Broadcast Telemetry: [0]=0xC9, [1]=0xC2, [2]=state */
static uint8_t s_mfg_data[3] = {0xC9, 0xC2, 0x00};

/* STRICT NUS RX Handler — Only accepts the 4 authorized commands */
static ssize_t on_nus_rx_write(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len,
                               uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    const char *cmd = (const char *)buf;
    if (len < 1) return len;

    /* 1. AED ON: 'A1' or "AED_ON" */
    if ((len >= 2 && cmd[0] == 'A' && cmd[1] == '1') ||
        (len >= 6 && memcmp(cmd, "AED_ON", 6) == 0)) {
        LOG_INF("[RX ALLOWED] AED ON command accepted");
        if (s_aed_mode_cb) s_aed_mode_cb(true);
        return len;
    }

    /* 2. AED OFF: 'A0' or "AED_OFF" */
    if ((len >= 2 && cmd[0] == 'A' && cmd[1] == '0') ||
        (len >= 7 && memcmp(cmd, "AED_OFF", 7) == 0)) {
        LOG_INF("[RX ALLOWED] AED OFF command accepted");
        if (s_aed_mode_cb) s_aed_mode_cb(false);
        return len;
    }

    /* 3. Shock ON (Arm): 'K1', 'E', or "SHOCK_ON" */
    if (cmd[0] == 'E' ||
        (len >= 2 && cmd[0] == 'K' && cmd[1] == '1') ||
        (len >= 8 && memcmp(cmd, "SHOCK_ON", 8) == 0)) {
        LOG_INF("[RX ALLOWED] Shock ON (Arm) command accepted");
        if (s_shock_cb) s_shock_cb(true);
        return len;
    }

    /* 4. Shock OFF (Disarm): 'K0', 'D', or "SHOCK_OFF" */
    if (cmd[0] == 'D' ||
        (len >= 2 && cmd[0] == 'K' && cmd[1] == '0') ||
        (len >= 9 && memcmp(cmd, "SHOCK_OFF", 9) == 0)) {
        LOG_INF("[RX ALLOWED] Shock OFF (Disarm) command accepted");
        if (s_shock_cb) s_shock_cb(false);
        return len;
    }

    /* All other incoming packets are strictly dropped */
    LOG_WRN("[RX BLOCKED] Ignored unauthorized packet: '%.*s'", len, cmd);
    return len;
}

static ssize_t on_nus_tx_read(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr,
                              void *buf, uint16_t len,
                              uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, s_tx_buffer, s_tx_len);
}

static void on_nus_tx_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    s_tx_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("AED NUS TX CCC notification %s", s_tx_notify_enabled ? "enabled" : "disabled");
}

/* GATT Service Definition */
BT_GATT_SERVICE_DEFINE(aed_nus_svc,
    BT_GATT_PRIMARY_SERVICE(&nus_uuid),
    BT_GATT_CHARACTERISTIC(&nus_rx_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_nus_rx_write, NULL),
    BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           on_nus_tx_read, NULL, NULL),
    BT_GATT_CCC(on_nus_tx_ccc_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* BLE Advertising Data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, AED_BLE_DEVICE_NAME, sizeof(AED_BLE_DEVICE_NAME) - 1),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, s_mfg_data, sizeof(s_mfg_data)),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SERVICE_VAL),
};

static int start_advertising(void)
{
    if (s_connected) return 0;

    bt_le_adv_stop();
    s_advertising = false;

    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("AED advertising failed (err %d)", err);
        return err;
    }

    s_advertising = true;
    LOG_INF("AED Advertising started as '%s'", AED_BLE_DEVICE_NAME);
    return 0;
}

static void adv_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    if (s_connected) return;
    int err = start_advertising();
    if (err) {
        k_work_reschedule(&s_adv_work, K_MSEC(500));
    }
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("AED connection failed (err 0x%02x)", err);
        k_work_reschedule(&s_adv_work, K_MSEC(50));
        return;
    }
    k_work_cancel_delayable(&s_adv_work);
    s_conn = bt_conn_ref(conn);
    s_connected = true;
    s_advertising = false;
    LOG_INF(">>> Peer connected to AED Controller! <<<");

    if (s_conn_cb) {
        s_conn_cb(true);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("<<< Peer disconnected from AED (reason 0x%02x)", reason);
    if (s_conn) {
        bt_conn_unref(s_conn);
        s_conn = NULL;
    }
    s_connected = false;
    s_advertising = false;
    s_tx_notify_enabled = false;

    if (s_conn_cb) {
        s_conn_cb(false);
    }

    k_work_reschedule(&s_adv_work, K_MSEC(50));
}

static void recycled(void)
{
    LOG_INF("AED connection recycled. Immediate advertising restart.");
    k_work_reschedule(&s_adv_work, K_NO_WAIT);
}

BT_CONN_CB_DEFINE(aed_conn_callbacks) = {
    .connected    = connected,
    .disconnected = disconnected,
    .recycled     = recycled,
};

int aed_ble_init(aed_ble_shock_cmd_cb_t shock_cb,
                 aed_ble_conn_cb_t conn_cb,
                 aed_ble_aed_mode_cb_t aed_mode_cb)
{
    s_shock_cb = shock_cb;
    s_conn_cb = conn_cb;
    s_aed_mode_cb = aed_mode_cb;
    k_work_init_delayable(&s_adv_work, adv_work_handler);

    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("BLE init failed (err %d)", err);
        return err;
    }

    LOG_INF("AED BLE subsystem initialized (Strict 4-Command Protocol Enforced)");
    return start_advertising();
}

bool aed_ble_is_connected(void)
{
    return s_connected;
}

/* =============================================================================
 * STRICT TRANSMISSION ENGINE — ONLY THESE 4 PACKETS ARE SENT
 * ============================================================================= */
static int send_strictly(const char *cmd, uint8_t state_code)
{
    if (!cmd) return -EINVAL;

    s_mfg_data[2] = state_code;

    if (s_connected && s_tx_notify_enabled) {
        size_t len = strlen(cmd);
        memcpy(s_tx_buffer, cmd, len);
        s_tx_len = (uint16_t)len;
        int ret = bt_gatt_notify(s_conn, &aed_nus_svc.attrs[3], cmd, len);
        LOG_INF("[TX SENT -> MANIKIN] '%.*s' (ret=%d)", (int)(len > 1 ? len - 1 : len), cmd, ret);
        return ret;
    } else {
        LOG_INF("[TX BUFFERED] '%s' (Waiting for NUS notify)", cmd);
    }

    return 0;
}

/* 1. Transmit AED ON */
int aed_ble_send_aed_on(void)
{
    return send_strictly("AED_ON\n", 0x01);
}

/* 2. Transmit AED OFF */
int aed_ble_send_aed_off(void)
{
    return send_strictly("AED_OFF\n", 0x00);
}

/* 3. Transmit Shock ON (Arm) */
int aed_ble_send_shock_on(void)
{
    return send_strictly("SHOCK_ON\n", 0x03);
}

/* 4. Transmit Shock OFF (Disarm) */
int aed_ble_send_shock_off(void)
{
    return send_strictly("SHOCK_OFF\n", 0x04);
}
