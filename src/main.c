/**
 * @file    main.c
 * @brief   CPR External AED Shock Unit — Main Application (Zephyr RTOS)
 * @version 2.1.0
 *
 * PROTOCOL: ESP-NOW ONLY (No BLE)
 * ─────────────────────────────────────────────────────────────────────────────
 * RGB LED INDICATION SYSTEM (Matching Clinical Resuscitation System):
 *   MODE 01: WAITING FOR PEER  - Deep Blue breathing (1.8s breath cycle, 5% - 100%)
 *   MODE 02: PEER ACTIVE&READY - Emerald Green continuous steady glow (100% Constant Solid ON)
 *   MODE 03: SHOCK ON (ALERT)  - Warm Amber/Yellow one-time triple strobe burst (3x 60ms ON / 60ms OFF)
 *   MODE 04: SHOCK GIVING(ARM) - Urgent Red rapid hazard strobe (5 Hz / 100ms ON / 100ms OFF)
 *   MODE 05: SHOCK OFF (DELIV) - Pure Green 1.0s confirmation latch -> returns to Peer Active Glow
 *
 * SHOCK SEQUENCE TIMING:
 *   Button Press -> Mode 03 Triple Strobe (~360ms) -> Mode 04 Red Hazard 5s -> Mode 05 Green Latch 1s -> Mode 02 Ready
 *
 * HARDWARE ASSIGNMENTS (ESP32-C3):
 *   Red: GPIO 3 | Green: GPIO 4 | Blue: GPIO 5 | Sync: GPIO 7 | Shock: GPIO 9
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <esp_mac.h>

#include "aed_config.h"
#include "aed_button.h"
#include "aed_led.h"
#include "aed_espnow.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ─── AED application state ──────────────────────────────────────────────── */
static volatile aed_state_t s_aed_state  = AED_STATE_DISARMED;
static volatile bool        s_aed_active = true;   /* false when power switch OFF */

/* ─── Shock sequence: Zephyr delayable work ──────────────────────────────── */
static struct k_work_delayable s_shock_seq_work;
static uint8_t s_shock_stage = 0;  /* 0=Idle, 1=Alert Strobe, 2=Red Hazard (5s) */

static void shock_sequence_work_handler(struct k_work *work);

/* ─────────────────────────────────────────────────────────────────────────── *
 * start_shock_sequence()
 * Mode 03 (Alert Triple Strobe) -> Mode 04 (Red Hazard 5s) -> Mode 05 (Green 1s)
 * ─────────────────────────────────────────────────────────────────────────── */
static void start_shock_sequence(void)
{
    if (!s_aed_active) {
        LOG_WRN("Shock sequence ignored: AED unit is inactive / OFF");
        return;
    }
    if (s_shock_stage != 0) {
        LOG_WRN("Shock sequence already active (Stage %d in progress)", s_shock_stage);
        return;
    }

    s_shock_stage = 1;
    s_aed_state   = AED_STATE_ARMED;

    LOG_INF("══════════════════════════════════════════════════════");
    LOG_INF(">>> [MODE 03: SHOCK ON (ALERT)] WARM AMBER BURST   <<<");
    LOG_INF(">>> Shock Advised / Internal Capacitors Charged    <<<");
    LOG_INF("══════════════════════════════════════════════════════");

    /* Mode 03: Warm Amber/Yellow Triple Strobe Burst (3x 60ms ON / 60ms OFF = ~360ms) */
    aed_led_set_mode(AED_LED_MODE_SHOCK_ON_ALERT);

    /* Notify Manikin: SHOCK_ON stage=1 */
    aed_espnow_send_shock_on(1);

    /* Auto-transitions to Mode 04 (Shock Giving / Red Hazard Strobe) after burst (~360ms) */
    k_work_reschedule(&s_shock_seq_work, K_MSEC(SHOCK_ALERT_TOTAL_BURST_MS));
}

/* ─────────────────────────────────────────────────────────────────────────── *
 * shock_sequence_work_handler()
 * Stage 1 (Alert Burst) -> Stage 2 (Delivering Red 5s) -> Stage 3 (Green Latch 1s)
 * ─────────────────────────────────────────────────────────────────────────── */
static void shock_sequence_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (s_shock_stage == 1) {
        /* Stage 1 (Alert Burst) complete -> Stage 2 (Delivering Red 5s) */
        s_shock_stage = 2;
        s_aed_state   = AED_STATE_SHOCK;

        LOG_INF("══════════════════════════════════════════════════════");
        LOG_INF(">>> [MODE 04: SHOCK GIVING] URGENT RED 5 Hz HAZARD <<<");
        LOG_INF(">>> Firing 1ms Sync Pulse + SHOCK_ON stage=2 sent  <<<");
        LOG_INF("══════════════════════════════════════════════════════");

        /* 1. Visual: Urgent Red rapid hazard strobe (5 Hz / 100ms ON / 100ms OFF) */
        aed_led_set_mode(AED_LED_MODE_SHOCK_GIVING);

        /* 2. Fire 1ms hardware sync pulse on GPIO 7 */
        aed_button_fire_sync_pulse(TIMING_SYNC_PULSE_MS * 1000U);

        /* 3. Notify Manikin: SHOCK_ON stage=2 (delivering) */
        aed_espnow_send_shock_on(2);

        /* Schedule Stage 3 in 5000 ms */
        k_work_reschedule(&s_shock_seq_work, K_MSEC(TIMING_SHOCK_GIVING_MS));
    }
    else if (s_shock_stage == 2) {
        /* Stage 2 (Red Hazard 5s) complete -> Stage 3 (Green Latch 1s) */
        s_shock_stage = 0;
        s_aed_state   = AED_STATE_DISARMED;

        LOG_INF("══════════════════════════════════════════════════════");
        LOG_INF(">>> [MODE 05: SHOCK OFF] PURE GREEN 1s CONFIRMATION <<<");
        LOG_INF(">>> Energy Safely Discharged — Resume CPR           <<<");
        LOG_INF("══════════════════════════════════════════════════════");

        /* 1. Visual: Pure Green 1.0s confirmation latch -> then returns to Mode 02 */
        aed_led_set_mode(AED_LED_MODE_SHOCK_OFF_LATCH);

        /* 2. Notify Manikin: SHOCK_OFF stage=3 */
        aed_espnow_send_shock_off();
    }
}

/* ─────────────────────────────────────────────────────────────────────────── *
 * BUTTON CALLBACKS
 * ─────────────────────────────────────────────────────────────────────────── */

/* Fired when physical shock button (GPIO 9) is pressed */
static void on_shock_button_pressed(void)
{
    LOG_INF(">>> AED SHOCK BUTTON PRESSED (GPIO 9) — Initiating Shock Sequence <<<");
    start_shock_sequence();
}

/* Fired when power switch (GPIO 6) toggles */
static void on_aed_power_switch_toggled(bool is_switch_on)
{
    s_aed_active = is_switch_on;

    if (s_aed_active) {
        LOG_INF(">>> AED CONTROLLER: POWER ON <<<");
        aed_espnow_send_aed_on();
        if (aed_espnow_is_ready()) {
            aed_led_set_mode(AED_LED_MODE_PEER_ACTIVE);
        } else {
            aed_led_set_mode(AED_LED_MODE_WAITING_FOR_PEER);
        }
    } else {
        LOG_INF("<<< AED CONTROLLER: POWER OFF >>>");

        /* Cancel any active shock sequence */
        k_work_cancel_delayable(&s_shock_seq_work);
        s_shock_stage = 0;
        s_aed_state   = AED_STATE_DISARMED;

        aed_espnow_send_aed_off();
        aed_led_set_mode(AED_LED_MODE_OFF);
    }
}

/* ─────────────────────────────────────────────────────────────────────────── *
 * ESP-NOW SENT CALLBACK
 * ─────────────────────────────────────────────────────────────────────────── */
static void on_espnow_sent(bool success)
{
    if (!success) {
        LOG_WRN("[ESP-NOW] Last packet delivery FAILED — Manikin may be out of range");
    }
}

/* ─────────────────────────────────────────────────────────────────────────── *
 * MAIN
 * ─────────────────────────────────────────────────────────────────────────── */
int main(void)
{
    LOG_INF("══════════════════════════════════════════════════════");
    LOG_INF(" CPR External AED Unit v%s [Zephyr RTOS]", AED_FW_VERSION_STR);
    LOG_INF(" Target  : ESP32-C3 Super Mini");
    LOG_INF(" Protocol: ESP-NOW ONLY (No BLE)");
    LOG_INF(" Red: GPIO 3 | Grn: GPIO 4 | Blu: GPIO 5 | Sync: GPIO 7");
    LOG_INF("══════════════════════════════════════════════════════");

    int ret;

    /* 1. Initialize LED subsystem (starts in Mode 01: Deep Blue Breathing 1.8s) */
    ret = aed_led_init();
    if (ret) {
        LOG_ERR("LED init failed (err %d)", ret);
    }

    /* 2. Initialize shock sequence delayable work */
    k_work_init_delayable(&s_shock_seq_work, shock_sequence_work_handler);

    /* 3. Initialize buttons: Shock (GPIO 9), Power Switch (GPIO 6), Sync (GPIO 7) */
    ret = aed_button_init(on_shock_button_pressed, on_aed_power_switch_toggled);
    if (ret) {
        LOG_ERR("Button init failed (err %d)", ret);
    }

    /* 4. Read physical power switch state at boot
     *    (Option 1 hardware-cut: board running = switch ON -> always true) */
    s_aed_active = aed_power_switch_is_on();
    if (!s_aed_active) {
        LOG_INF("AED Controller started in POWER OFF state");
        aed_led_set_mode(AED_LED_MODE_OFF);
    }

    /* 5. Initialize ESP-NOW subsystem */
    ret = aed_espnow_init(on_espnow_sent);
    if (ret) {
        LOG_ERR("ESP-NOW init failed (err %d) — running without wireless", ret);
    }

    /* 6. On successful peer registration: transition to MODE 02 (PEER ACTIVE & READY / Solid Green) */
    if (s_aed_active && aed_espnow_is_ready()) {
        aed_espnow_send_aed_on();
        aed_led_set_mode(AED_LED_MODE_PEER_ACTIVE);
        LOG_INF("══════════════════════════════════════════════════════");
        LOG_INF(">>> [MODE 02: PEER ACTIVE & READY] SOLID GREEN     <<<");
        LOG_INF(">>> Wireless Telemetry Synced — Unit in Safe Standby<<<");
        LOG_INF("══════════════════════════════════════════════════════");
    } else if (s_aed_active) {
        /* If peer not yet active, remain in Mode 01 Deep Blue Breathing */
        aed_led_set_mode(AED_LED_MODE_WAITING_FOR_PEER);
        LOG_INF(">>> [MODE 01: WAITING FOR PEER] DEEP BLUE BREATHING <<<");
    }

    LOG_INF("AED System Ready. Press Shock Button (GPIO 9) to deliver shock sequence.");

    /* 7. Heartbeat loop */
    while (1) {
        const char *led_mode_str = "UNKNOWN";
        switch (aed_led_get_mode()) {
            case AED_LED_MODE_OFF:              led_mode_str = "OFF"; break;
            case AED_LED_MODE_WAITING_FOR_PEER: led_mode_str = "MODE 01: WAITING (BLUE BREATH)"; break;
            case AED_LED_MODE_PEER_ACTIVE:       led_mode_str = "MODE 02: PEER ACTIVE (SOLID GREEN)"; break;
            case AED_LED_MODE_SHOCK_ON_ALERT:   led_mode_str = "MODE 03: ALERT (AMBER BURST)"; break;
            case AED_LED_MODE_SHOCK_GIVING:     led_mode_str = "MODE 04: SHOCK (RED 5Hz)"; break;
            case AED_LED_MODE_SHOCK_OFF_LATCH:  led_mode_str = "MODE 05: COMPLETE (GREEN LATCH)"; break;
        }

        LOG_INF("[AED HEALTH] State: %s | LED: %s | Active: %s | ESP-NOW: %s",
                (s_aed_state == AED_STATE_ARMED) ? "ARMED"   :
                (s_aed_state == AED_STATE_SHOCK)  ? "SHOCK"   : "STANDBY",
                led_mode_str,
                s_aed_active              ? "YES"       : "NO",
                aed_espnow_is_ready()     ? "READY"     : "NOT INIT");

        k_msleep(5000);
    }

    return 0;
}
