/**
 * @file    aed_ble.c
 * @brief   AED BLE Peripheral Subsystem Implementation (Zephyr RTOS)
 * @version 4.0.0
 *
 * ROLE: BLE PERIPHERAL. The mobile App connects directly to the AED.
 *
 * Flow:
 *   1. AED boots -> advertises as "AED_001" (connectable)
 *   2. App connects, pairs (passkey, LE Secure Connections), bonds
 *   3. App enables NUS TX notify -> AED sends current power / shock values
 *   4. App writes commands to NUS RX:  C ?  P0 P1  S0 S1
 *   5. AED notifies events (power, shock) only when a value changes
 *
 * SECURITY:
 *   - LE Secure Connections only (CONFIG_BT_SMP_SC_ONLY)
 *   - Fixed per-unit passkey (AED_BLE_PASSKEY in aed_config.h) -> MITM-protected
 *   - All GATT attributes need an authenticated + encrypted link (PERM_*_AUTHEN)
 *   - Bond stored in flash (CONFIG_BT_SETTINGS)
 */

#include "aed_ble.h"
#include "aed_config.h"
#include "aed_protocol.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>

LOG_MODULE_REGISTER(aed_ble, LOG_LEVEL_INF);

/* --- NUS UUIDs ------------------------------------------------------------ */
#define BT_UUID_NUS_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x6e400001, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
#define BT_UUID_NUS_RX_VAL \
    BT_UUID_128_ENCODE(0x6e400002, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
#define BT_UUID_NUS_TX_VAL \
    BT_UUID_128_ENCODE(0x6e400003, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)

static struct bt_uuid_128 nus_uuid    = BT_UUID_INIT_128(BT_UUID_NUS_SERVICE_VAL);
static struct bt_uuid_128 nus_rx_uuid = BT_UUID_INIT_128(BT_UUID_NUS_RX_VAL);
static struct bt_uuid_128 nus_tx_uuid = BT_UUID_INIT_128(BT_UUID_NUS_TX_VAL);

/* --- State ---------------------------------------------------------------- */
static struct bt_conn *s_conn = NULL;
static volatile bool   s_connected = false;
static volatile bool   s_notify_en = false;

static aed_ble_cmd_cb_t    s_cmd_cb    = NULL;
static aed_ble_link_cb_t   s_link_cb   = NULL;
static aed_ble_notify_cb_t s_notify_cb = NULL;

static struct k_work_delayable s_adv_restart_work;
static struct k_spinlock       s_lock;
static atomic_t                s_packet_counter;
static uint8_t                 s_device_id[3] = {0, 0, 0};

/* --- Advertisement payload ------------------------------------------------ *
 * Manufacturer data: C4 C9 (company 0xC9C4) + state (0 available, 1 connected)
 */
static uint8_t s_mfg_data[3] = {
    (uint8_t)(AED_BLE_MFG_COMPANY_ID & 0xFF),
    (uint8_t)((AED_BLE_MFG_COMPANY_ID >> 8) & 0xFF),
    AED_BLE_STATE_AVAILABLE
};

static const struct bt_data s_ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, AED_BLE_DEVICE_NAME, sizeof(AED_BLE_DEVICE_NAME) - 1),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, s_mfg_data, sizeof(s_mfg_data)),
};

static const struct bt_data s_sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SERVICE_VAL),
};

/* --- GATT: NUS TX (notify: AED -> App) ------------------------------------ */
static uint8_t  s_tx_buf[24];
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
    if (s_notify_en && s_notify_cb) {
        s_notify_cb();
    }
}

/* --- GATT: NUS RX (write: App -> AED) ------------------------------------- *
 * Commands (1 letter + optional digit, trailing CR/LF ignored):
 *   C or ?   announce      P0 / P1   power button      S0 / S1   shock button
 */
static ssize_t on_nus_rx_write(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len,
                               uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len < 1 || conn != s_conn) {
        return len;
    }

    const char *cmd = (const char *)buf;
    char c = cmd[0];
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    char arg = (len >= 2) ? cmd[1] : '\0';

    aed_cmd_t out = 0;

    if (c == 'C' || c == '?') {
        out = AED_CMD_ANNOUNCE;
    } else if (c == 'P' && arg == '0') {
        out = AED_CMD_P0;
    } else if (c == 'P' && arg == '1') {
        out = AED_CMD_P1;
    } else if (c == 'S' && arg == '0') {
        out = AED_CMD_S0;
    } else if (c == 'S' && arg == '1') {
        out = AED_CMD_S1;
    } else {
        LOG_WRN("[RX BLOCKED] Unknown command from App (len %u, first byte 0x%02x)", len, (uint8_t)cmd[0]);
        return len;
    }

    LOG_INF("[RX] Command %d from App", (int)out);
    if (s_cmd_cb) {
        s_cmd_cb(out);
    }
    return len;
}

/* --- GATT Service (all attributes need authenticated + encrypted link) ---- */
BT_GATT_SERVICE_DEFINE(aed_nus_svc,
    BT_GATT_PRIMARY_SERVICE(&nus_uuid),

    /* attrs[1], attrs[2]: NUS RX - App writes commands here */
    BT_GATT_CHARACTERISTIC(&nus_rx_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_AUTHEN,
                           NULL, on_nus_rx_write, NULL),

    /* attrs[3], attrs[4]: NUS TX - AED notifies events here */
    BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ_AUTHEN,
                           on_nus_tx_read, NULL, NULL),

    /* attrs[5]: CCC */
    BT_GATT_CCC(on_nus_tx_ccc_changed,
                BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN),
);

#define NUS_TX_ATTR_INDEX 4

/* --- Packet send ---------------------------------------------------------- */
static int send_bytes(const void *data, uint16_t len)
{
    k_spinlock_key_t key = k_spin_lock(&s_lock);
    struct bt_conn *conn = s_conn ? bt_conn_ref(s_conn) : NULL;
    k_spin_unlock(&s_lock, key);

    if (!conn) {
        return -ENOTCONN;
    }

    const struct bt_gatt_attr *tx = &aed_nus_svc.attrs[NUS_TX_ATTR_INDEX];
    int err;

    if (!bt_gatt_is_subscribed(conn, tx, BT_GATT_CCC_NOTIFY)) {
        err = -ENOTCONN;
    } else if (len > sizeof(s_tx_buf)) {
        err = -EMSGSIZE;
    } else {
        memcpy(s_tx_buf, data, len);
        s_tx_len = len;
        err = bt_gatt_notify(conn, tx, data, len);
    }

    bt_conn_unref(conn);
    return err;
}

int aed_ble_send_event(uint8_t type, int8_t value)
{
    AedEventPacket p;
    memset(&p, 0, sizeof(p));

    p.magic[0]    = PACKET_MAGIC_0;
    p.magic[1]    = PACKET_MAGIC_1;
    p.version     = PROTOCOL_VER;
    p.type        = type;
    p.timestamp   = k_uptime_get_32();
    p.id          = (uint16_t)atomic_inc(&s_packet_counter);
    p.value       = value;
    memcpy(p.deviceID, s_device_id, sizeof(p.deviceID));
    p.checksum    = aed_proto_checksum((const uint8_t *)&p, sizeof(p) - 1u);

    int err = send_bytes(&p, sizeof(p));
    if (err) {
        LOG_WRN("[AED BLE] event 0x%02x=%d not sent (err %d)", type, value, err);
    }
    return err;
}

int aed_ble_send_announce(void)
{
    AedAnnouncePacket p;
    memset(&p, 0, sizeof(p));

    p.magic[0]   = PACKET_MAGIC_0;
    p.magic[1]   = PACKET_MAGIC_1;
    p.version    = PROTOCOL_VER;
    p.type       = PTYPE_ANNOUNCE;
    p.timestamp  = k_uptime_get_32();
    p.id         = (uint16_t)atomic_inc(&s_packet_counter);
    p.battery    = -1;   /* AED has no battery measurement */
    memcpy(p.deviceID, s_device_id, sizeof(p.deviceID));
    /* AED_BLE_DEVICE_NAME is "AED_001": send the unit id part "001" */
    memcpy(p.name, AED_BLE_DEVICE_NAME + 4, 3);
    p.name[3]    = '\0';
    p.deviceType = AED_DEVICE_TYPE;
    p.checksum   = aed_proto_checksum((const uint8_t *)&p, sizeof(p) - 1u);

    return send_bytes(&p, sizeof(p));
}

/* --- Advertising ---------------------------------------------------------- */
static int start_advertising(void)
{
    if (s_connected) {
        return 0;
    }

    bt_le_adv_stop();

    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                              s_ad, ARRAY_SIZE(s_ad),
                              s_sd, ARRAY_SIZE(s_sd));
    if (err) {
        LOG_ERR("[AED BLE] bt_le_adv_start failed (err %d)", err);
        return err;
    }

    LOG_INF("[AED BLE] Advertising as '%s' - waiting for App...", AED_BLE_DEVICE_NAME);
    return 0;
}

static void adv_restart_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    if (!s_connected && start_advertising()) {
        k_work_reschedule(&s_adv_restart_work, K_MSEC(500));
    }
}

/* --- Connection callbacks ------------------------------------------------- */
static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("[AED BLE] Connection failed (err 0x%02x) - restarting adv", err);
        k_work_reschedule(&s_adv_restart_work, K_MSEC(100));
        return;
    }

    k_work_cancel_delayable(&s_adv_restart_work);

    k_spinlock_key_t key = k_spin_lock(&s_lock);
    s_conn      = bt_conn_ref(conn);
    s_connected = true;
    k_spin_unlock(&s_lock, key);

    s_mfg_data[2] = AED_BLE_STATE_CONNECTED;
    LOG_INF(">>> [AED BLE] App connected");

    /* Ask the App to secure the link (passkey pairing / encryption) */
    int sec = bt_conn_set_security(conn, BT_SECURITY_L4);
    if (sec) {
        LOG_WRN("[AED BLE] bt_conn_set_security failed (err %d)", sec);
    }

    if (s_link_cb) {
        s_link_cb(true);
    }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("<<< [AED BLE] App disconnected (reason 0x%02x)", reason);

    k_spinlock_key_t key = k_spin_lock(&s_lock);
    struct bt_conn *old = s_conn;
    s_conn      = NULL;
    s_connected = false;
    k_spin_unlock(&s_lock, key);

    if (old) {
        bt_conn_unref(old);
    }
    s_notify_en   = false;
    s_mfg_data[2] = AED_BLE_STATE_AVAILABLE;

    if (s_link_cb) {
        s_link_cb(false);
    }

    k_work_reschedule(&s_adv_restart_work, K_MSEC(100));
}

static void on_recycled(void)
{
    k_work_reschedule(&s_adv_restart_work, K_NO_WAIT);
}

BT_CONN_CB_DEFINE(aed_conn_callbacks) = {
    .connected    = on_connected,
    .disconnected = on_disconnected,
    .recycled     = on_recycled,
};

/* --- Pairing (fixed passkey, App enters it) ------------------------------- */
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(passkey);
    /* Passkey is fixed (AED_BLE_PASSKEY) and printed on the unit label */
    LOG_INF("[AED BLE] Pairing: App must enter the unit passkey");
}

static void auth_cancel(struct bt_conn *conn)
{
    ARG_UNUSED(conn);
    LOG_WRN("[AED BLE] Pairing cancelled");
}

#if defined(CONFIG_BT_APP_PASSKEY)
static uint32_t auth_app_passkey(struct bt_conn *conn)
{
    ARG_UNUSED(conn);
    return (uint32_t)AED_BLE_PASSKEY;
}
#endif

static struct bt_conn_auth_cb s_auth_cb = {
    .passkey_display = auth_passkey_display,
#if defined(CONFIG_BT_APP_PASSKEY)
    .app_passkey     = auth_app_passkey,
#endif
    .cancel          = auth_cancel,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
    ARG_UNUSED(conn);
    LOG_INF("[AED BLE] Pairing complete (bonded=%d)", bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    ARG_UNUSED(conn);
    LOG_WRN("[AED BLE] Pairing failed (reason %d)", reason);
}

static struct bt_conn_auth_info_cb s_auth_info_cb = {
    .pairing_complete = pairing_complete,
    .pairing_failed   = pairing_failed,
};

/* --- Public API ----------------------------------------------------------- */
int aed_ble_init(aed_ble_cmd_cb_t cmd_cb,
                 aed_ble_link_cb_t link_cb,
                 aed_ble_notify_cb_t notify_cb)
{
    s_cmd_cb    = cmd_cb;
    s_link_cb   = link_cb;
    s_notify_cb = notify_cb;

    k_work_init_delayable(&s_adv_restart_work, adv_restart_work_handler);

    int err = bt_conn_auth_cb_register(&s_auth_cb);
    if (err) {
        LOG_ERR("[AED BLE] auth cb register failed (err %d)", err);
        return err;
    }
    err = bt_conn_auth_info_cb_register(&s_auth_info_cb);
    if (err) {
        LOG_ERR("[AED BLE] auth info cb register failed (err %d)", err);
        return err;
    }

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("[AED BLE] bt_enable failed (err %d)", err);
        return err;
    }

    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
        err = settings_load();
        if (err) {
            LOG_WRN("[AED BLE] settings_load failed (err %d) - bond not persistent", err);
        }
    }

    /* Device ID = last 3 bytes of the BLE address */
    bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
    size_t count = ARRAY_SIZE(addrs);
    bt_id_get(addrs, &count);
    if (count > 0) {
        s_device_id[0] = addrs[0].a.val[2];
        s_device_id[1] = addrs[0].a.val[1];
        s_device_id[2] = addrs[0].a.val[0];
    }

    LOG_INF("[AED BLE] BLE stack enabled. Starting advertising as '%s'...", AED_BLE_DEVICE_NAME);
    return start_advertising();
}

bool aed_ble_is_connected(void)
{
    return s_connected;
}
