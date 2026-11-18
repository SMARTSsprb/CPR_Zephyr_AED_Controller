/**
 * @file    main.c
 * @brief   CPR External AED Shock Unit — Main Application (Zephyr RTOS)
 * @version 2.0.0
 *
 * PROTOCOL: ESP-NOW ONLY (No BLE)
 * ─────────────────────────────────────────────────────────────────────────────
 * STRICT 4-COMMAND PROTOCOL (Sends to Manikin via ESP-NOW):
 *   cmd=1 stage=0  → AED_ON    (sent on boot / power-on)
 *   cmd=0 stage=0  → AED_OFF   (sent on power-off)
 *   cmd=2 stage=1  → SHOCK_ON  (Stage 1: Yellow / Instruction, 5s)
 *   cmd=2 stage=2  → SHOCK_ON  (Stage 2: Red / Delivering, 5s)
 *   cmd=3 stage=3  → SHOCK_OFF (Stage 3: Complete)
 *
 * SHOCK SEQUENCE:
 *   Button Press → Stage 1 Yellow 5s → Stage 2 Red 5s → Stage 3 Green → Standby
 *
 * TARGET MANIKIN MAC: E8:3D:C1:82:1B:F0 (defined in aed_config.h)
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
static uint8_t s_shock_stage = 0;  /* 0=Idle, 1=Yellow(5s), 2=Red(5s) */

static void shock_sequence_work_handler(struct k_work *work);

/* ─────────────────────────────────────────────────────────────────────────── *
 * start_shock_sequence()
 * Initiates: Stage1 Yellow(5s) → Stage2 Red(5s) → Stage3 Green
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
    LOG_INF(">>> [SHOCK STAGE 1/3] INSTRUCTION (YELLOW 5s) <<<");
    LOG_INF(">>> Rhythm Analysis / Charging Phase Active      <<<");
    LOG_INF("══════════════════════════════════════════════════════");

    /* Yellow LED — instruction phase */
    aed_led_set_mode(AED_LED_MODE_SHOCK_INSTRUCTION);

    /* Notify Manikin: SHOCK_ON stage=1 */
    aed_espnow_send_shock_on(1);

    /* Schedule Stage 2 in 5000 ms */
    k_work_reschedule(&s_shock_seq_work, K_MSEC(TIMING_SHOCK_INSTRUCTION_MS));
}

/* ─────────────────────────────────────────────────────────────────────────── *
 * shock_sequence_work_handler()
 * Stage 1 → 2 → 3 transitions, fired by k_work_delayable
 * ─────────────────────────────────────────────────────────────────────────── */
static void shock_sequence_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (s_shock_stage == 1) {
        /* Stage 1 (Yellow) done → Stage 2 (Red 5s) */
        s_shock_stage = 2;
        s_aed_state   = AED_STATE_SHOCK;

        LOG_INF("══════════════════════════════════════════════════════");
        LOG_INF(">>> [SHOCK STAGE 2/3] DELIVERING SHOCK (RED 5s)  <<<");
        LOG_INF(">>> 1ms Sync Pulse + SHOCK_ON stage=2 sent        <<<");
        LOG_INF("══════════════════════════════════════════════════════");

        /* 1. Visual: Urgent Red strobe */
        aed_led_set_mode(AED_LED_MODE_SHOCK_GIVING);

        /* 2. Fire 1ms hardware sync pulse on GPIO 7 */
        aed_button_fire_sync_pulse(TIMING_SYNC_PULSE_MS * 1000U); /* convert ms→us */

        /* 3. Notify Manikin: SHOCK_ON stage=2 (delivering) */
        aed_espnow_send_shock_on(2);

        /* Schedule Stage 3 in 5000 ms */
        k_work_reschedule(&s_shock_seq_work, K_MSEC(TIMING_SHOCK_GIVING_MS));
    }
    else if (s_shock_stage == 2) {
        /* Stage 2 (Red) done → Stage 3 (Green / complete) */
        s_shock_stage = 0;
        s_aed_state   = AED_STATE_DISARMED;

        LOG_INF("══════════════════════════════════════════════════════");
        LOG_INF(">>> [SHOCK STAGE 3/3] SHOCK COMPLETE (GREEN)      <<<");
        LOG_INF(">>> Safe to Touch Patient — Resume CPR             <<<");
        LOG_INF("══════════════════════════════════════════════════════");

        /* 1. Visual: 1s solid Green latch, then back to Blue breathing */
        aed_led_set_mode(AED_LED_MODE_SHOCK_OFF_PULSE);

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
    LOG_INF(">>> AED SHOCK BUTTON PRESSED (GPIO 9) — Starting Shock Flow <<<");
    start_shock_sequence();
}

/* Fired when power switch (GPIO 6) toggles (Option 2: logic switch only) */
static void on_aed_power_switch_toggled(bool is_switch_on)
{
    s_aed_active = is_switch_on;

    if (s_aed_active) {
        LOG_INF(">>> AED CONTROLLER: ON <<<");
        aed_espnow_send_aed_on();
        aed_led_set_mode(AED_LED_MODE_WAITING_FOR_CONN);
    } else {
        LOG_INF("<<< AED CONTROLLER: OFF >>>");

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
    LOG_INF("══════════════════════════════════════════════════════");

    int ret;

    /* 1. Initialize LED subsystem (starts in Deep Blue Breathing) */
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
     *    (Option 1 hardware-cut: board running = switch ON → always true) */
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

    /* 6. Send AED_ON immediately on boot (hardware cut: always powered when running) */
    if (s_aed_active && aed_espnow_is_ready()) {
        aed_espnow_send_aed_on();
        aed_led_set_mode(AED_LED_MODE_WAITING_FOR_CONN);
    }

    LOG_INF("AED System Ready. Press Shock Button (GPIO 9) to deliver shock sequence.");

    /* 7. Heartbeat loop */
    while (1) {
        LOG_INF("[AED HEALTH] State: %s | Active: %s | ESP-NOW: %s",
                (s_aed_state == AED_STATE_ARMED) ? "ARMED"   :
                (s_aed_state == AED_STATE_SHOCK)  ? "SHOCK"   : "STANDBY",
                s_aed_active              ? "YES"       : "NO",
                aed_espnow_is_ready()     ? "READY"     : "NOT INIT");

        k_msleep(5000);
    }

    return 0;
}
