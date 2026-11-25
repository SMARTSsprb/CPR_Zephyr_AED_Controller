/**
 * @file    main.c
 * @brief   CPR External AED Shock Unit - Main Application (Zephyr RTOS)
 * @version 4.0.0
 *
 * The mobile App is the BLE central and connects directly to this AED.
 *
 * App commands (NUS RX):   C or ?  |  P0 P1  |  S0 S1
 * AED events   (NUS TX):   power 0/1/2   shock 0/1/2/3   (only on change)
 *
 *   power: 0 button off | 1 waiting for press | 2 power on
 *   shock: 0 button off | 1 waiting for press | 2 in progress | 3 delivered
 *   (power 0 always forces shock 0)
 *
 * Session flow:
 *   App P1            -> power 1   (power button enabled, nothing else happens)
 *   user presses power-> power 2   LED solid yellow
 *   App checks pads (Manikin), sends S1 -> shock 1 (shock button enabled)
 *   user presses shock-> shock 2   LED yellow 5 s -> red strobe 5 s (+ sync pulse)
 *   sequence done     -> shock 3   LED green
 *   App sends P0      -> power 0, shock 0, LED off
 *   App link lost     -> power 0, shock 0 (fail-safe)
 *
 * All state changes run in ONE context (system work queue): button ISRs and
 * BLE callbacks only post an event to the queue.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "aed_config.h"
#include "aed_protocol.h"
#include "aed_button.h"
#include "aed_led.h"
#include "aed_ble.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* --- Events (posted from ISR / BLE thread, handled in the work queue) ----- */
enum aed_event {
    EV_PWR_PRESS = 1,
    EV_SHOCK_PRESS,
    EV_P0,
    EV_P1,
    EV_S0,
    EV_S1,
    EV_ANNOUNCE,
    EV_LINK_UP,
    EV_LINK_DOWN,
    EV_NOTIFY_ON,
};

K_MSGQ_DEFINE(s_evq, sizeof(uint8_t), 16, 1);
static struct k_work s_ev_work;

static void post_event(enum aed_event ev)
{
    uint8_t e = (uint8_t)ev;
    if (k_msgq_put(&s_evq, &e, K_NO_WAIT) == 0) {
        k_work_submit(&s_ev_work);
    } else {
        LOG_WRN("Event queue full, event %d dropped", (int)ev);
    }
}

/* --- AED state ------------------------------------------------------------ */
static uint8_t s_power = AED_POWER_OFF;
static uint8_t s_shock = AED_SHOCK_OFF;

static struct k_work_delayable s_shock_seq_work;
static uint8_t s_shock_stage = 0;  /* 0 idle, 1 yellow (5 s), 2 red strobe (5 s) */

static void publish_power(bool force)
{
    static int8_t last = -1;
    if (force || last != (int8_t)s_power) {
        last = (int8_t)s_power;
        aed_ble_send_event(PTYPE_AED_POWER, (int8_t)s_power);
    }
}

static void publish_shock(bool force)
{
    static int8_t last = -1;
    if (force || last != (int8_t)s_shock) {
        last = (int8_t)s_shock;
        aed_ble_send_event(PTYPE_AED_SHOCK, (int8_t)s_shock);
    }
}

/* LED follows the state, except while the shock sequence drives it */
static void apply_led(void)
{
    if (!aed_ble_is_connected()) {
        aed_led_set_mode(AED_LED_MODE_WAITING_FOR_PEER);      /* blue breathing */
    } else if (s_shock == AED_SHOCK_IN_PROGRESS) {
        /* sequence owns the LED */
    } else if (s_shock == AED_SHOCK_DELIVERED) {
        aed_led_set_mode(AED_LED_MODE_PEER_ACTIVE);            /* solid green */
    } else if (s_power == AED_POWER_ON) {
        aed_led_set_mode(AED_LED_MODE_POWER_READY);            /* solid yellow */
    } else {
        aed_led_set_mode(AED_LED_MODE_OFF);
    }
}

static void cancel_sequence(void)
{
    k_work_cancel_delayable(&s_shock_seq_work);
    s_shock_stage = 0;
}

/* Power 0 + shock 0 (App P0, user power-off, link lost) */
static void all_off(bool publish)
{
    cancel_sequence();
    s_power = AED_POWER_OFF;
    s_shock = AED_SHOCK_OFF;
    if (publish) {
        publish_power(true);
        publish_shock(true);
    }
    apply_led();
}

/* --- Shock sequence: yellow 5 s -> red strobe 5 s -> green (delivered) ---- */
static void start_shock_sequence(void)
{
    s_shock       = AED_SHOCK_IN_PROGRESS;
    s_shock_stage = 1;
    publish_shock(false);

    LOG_INF(">>> [SHOCK 1/2] YELLOW 5 s <<<");
    aed_led_set_mode(AED_LED_MODE_SHOCK_INSTRUCTION);
    k_work_reschedule(&s_shock_seq_work, K_MSEC(TIMING_SHOCK_INSTRUCTION_MS));
}

static void shock_sequence_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (s_shock_stage == 1) {
        s_shock_stage = 2;
        LOG_INF(">>> [SHOCK 2/2] RED STROBE 5 s, sync pulse on GPIO 7 <<<");
        aed_led_set_mode(AED_LED_MODE_SHOCK_GIVING);
        aed_button_fire_sync_pulse(TIMING_SYNC_PULSE_MS * 1000U);
        k_work_reschedule(&s_shock_seq_work, K_MSEC(TIMING_SHOCK_GIVING_MS));
    } else if (s_shock_stage == 2) {
        s_shock_stage = 0;
        s_shock       = AED_SHOCK_DELIVERED;
        LOG_INF(">>> SHOCK DELIVERED - waiting for App P0 <<<");
        aed_led_set_mode(AED_LED_MODE_SHOCK_OFF_LATCH);        /* green */
        publish_shock(false);
    }
}

/* --- Event handler (single context) --------------------------------------- */
static void handle_event(uint8_t ev)
{
    switch (ev) {
    case EV_LINK_UP:
        LOG_INF("App connected");
        apply_led();
        break;

    case EV_LINK_DOWN:
        LOG_INF("App disconnected - fail-safe: power 0, shock 0");
        all_off(false);                 /* link is gone, nothing to publish */
        break;

    case EV_NOTIFY_ON:                  /* snapshot of the current values */
        publish_power(true);
        publish_shock(true);
        break;

    case EV_ANNOUNCE:
        aed_ble_send_announce();
        break;

    case EV_P1:                         /* enable power button */
        if (s_power == AED_POWER_OFF) {
            s_power = AED_POWER_WAIT_PRESS;
            apply_led();
        }
        publish_power(true);
        break;

    case EV_P0:                         /* disable power + shock buttons */
        all_off(true);
        break;

    case EV_S1:                         /* enable shock button (power must be on) */
        if (s_power == AED_POWER_ON && s_shock == AED_SHOCK_OFF) {
            s_shock = AED_SHOCK_WAIT_PRESS;
            apply_led();
        } else {
            LOG_WRN("S1 ignored (power=%u shock=%u)", s_power, s_shock);
        }
        publish_shock(true);
        break;

    case EV_S0:                         /* disable shock button */
        if (s_shock == AED_SHOCK_WAIT_PRESS) {
            s_shock = AED_SHOCK_OFF;
            apply_led();
        } else {
            LOG_WRN("S0 ignored (shock=%u)", s_shock);
        }
        publish_shock(true);
        break;

    case EV_PWR_PRESS:                  /* user presses the power button */
        if (!aed_ble_is_connected()) {
            break;
        }
        if (s_power == AED_POWER_WAIT_PRESS) {
            s_power = AED_POWER_ON;
            LOG_INF("Power button pressed: power ON (yellow)");
            apply_led();
            publish_power(false);
        } else if (s_power == AED_POWER_ON && s_shock != AED_SHOCK_IN_PROGRESS) {
            LOG_INF("Power button pressed: power OFF");
            all_off(true);
        } else {
            LOG_INF("Power button ignored (power=%u shock=%u)", s_power, s_shock);
        }
        break;

    case EV_SHOCK_PRESS:                /* user presses the shock button */
        if (s_shock == AED_SHOCK_WAIT_PRESS) {
            LOG_INF("Shock button pressed: starting sequence");
            start_shock_sequence();
        } else {
            LOG_INF("Shock button ignored (shock=%u)", s_shock);
        }
        break;

    default:
        break;
    }
}

static void event_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    uint8_t ev;
    while (k_msgq_get(&s_evq, &ev, K_NO_WAIT) == 0) {
        handle_event(ev);
    }
}

/* --- Callbacks: only post events ------------------------------------------ */
static void on_shock_button_pressed(void)  { post_event(EV_SHOCK_PRESS); }
static void on_power_button_pressed(void)  { post_event(EV_PWR_PRESS); }
static void on_app_link(bool connected)    { post_event(connected ? EV_LINK_UP : EV_LINK_DOWN); }
static void on_app_notify_enabled(void)    { post_event(EV_NOTIFY_ON); }

static void on_app_command(aed_cmd_t cmd)
{
    switch (cmd) {
    case AED_CMD_ANNOUNCE: post_event(EV_ANNOUNCE); break;
    case AED_CMD_P0:       post_event(EV_P0);       break;
    case AED_CMD_P1:       post_event(EV_P1);       break;
    case AED_CMD_S0:       post_event(EV_S0);       break;
    case AED_CMD_S1:       post_event(EV_S1);       break;
    default: break;
    }
}

/* --- MAIN ------------------------------------------------------------------ */
int main(void)
{
    LOG_INF("==================================================");
    LOG_INF(" CPR External AED Unit v%s [Zephyr RTOS]", AED_FW_VERSION_STR);
    LOG_INF(" Protocol: BLE PERIPHERAL (App connects directly)");
    LOG_INF(" Device  : %s", AED_BLE_DEVICE_NAME);
    LOG_INF("==================================================");

    k_work_init(&s_ev_work, event_work_handler);
    k_work_init_delayable(&s_shock_seq_work, shock_sequence_work_handler);

    int ret = aed_led_init();                       /* starts blue breathing */
    if (ret) {
        LOG_ERR("LED init failed (err %d)", ret);
    }

    ret = aed_button_init(on_shock_button_pressed, on_power_button_pressed);
    if (ret) {
        LOG_ERR("Button init failed (err %d)", ret);
    }

    ret = aed_ble_init(on_app_command, on_app_link, on_app_notify_enabled);
    if (ret) {
        LOG_ERR("BLE init failed (err %d) - running without wireless", ret);
    }

    LOG_INF("AED System Ready (power 0, shock 0).");

    while (1) {
        LOG_INF("[AED HEALTH] power=%u shock=%u | App: %s",
                s_power, s_shock,
                aed_ble_is_connected() ? "CONNECTED" : "ADVERTISING");
        k_msleep(5000);
    }

    return 0;
}
