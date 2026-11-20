/**
 * @file    main.c
 * @brief   CPR External AED Shock Unit — Main Application (Zephyr RTOS)
 * @version 3.0.0
 *
 * PROTOCOL: BLE PERIPHERAL ONLY
 * ─────────────────────────────────────────────────────────────────────────────
 * Flow:
 *   Boot   → LED Mode 01 (Deep Blue Breathing) + BLE advertising as "AED_001"
 *   App    → scans, sees "AED_001" in advertisement list, assigns it to a Manikin
 *   App    → sends assignment to Manikin over App↔Manikin BLE link
 *   Manikin→ connects to AED as BLE Central
 *   Connect→ LED Mode 02 (Solid Green)   [Manikin paired & ready]
 *   Shock  → Manikin writes SHOCK_ON → Mode 03 → Mode 04 → Mode 05 → Mode 02
 *   Discon → LED Mode 01 (Blue Breathing) + restart advertising
 *
 * RGB LED INDICATION SYSTEM:
 *   MODE 01: WAITING FOR PEER  - Deep Blue breathing (3.0s / 150 steps / 20ms)
 *   MODE 02: PEER ACTIVE&READY - Emerald Green continuous steady glow
 *   MODE 03: SHOCK ON (ALERT)  - Warm Amber/Yellow one-time triple strobe burst
 *   MODE 04: SHOCK GIVING(ARM) - Urgent Red rapid hazard strobe (5 Hz)
 *   MODE 05: SHOCK OFF (DELIV) - Pure Green 1.0s confirmation latch → Mode 02
 *
 * HARDWARE ASSIGNMENTS (ESP32-C3):
 *   Red: GPIO 3 | Green: GPIO 4 | Blue: GPIO 5 | Sync: GPIO 7 | Shock: GPIO 9
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "aed_config.h"
#include "aed_button.h"
#include "aed_led.h"
#include "aed_ble.h"

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
 * Mode 03 (Alert Triple Strobe) → Mode 04 (Red Hazard 5s) → Mode 05 (Green 1s)
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

    /* Update BLE advertisement state so App can see shock is in progress */
    aed_ble_update_adv_state(AED_BLE_STATE_SHOCK);

    LOG_INF("══════════════════════════════════════════════════════");
    LOG_INF(">>> [SHOCK STAGE 1/3] INSTRUCTION GIVING (YELLOW 5s) <<<");
    LOG_INF(">>> Instruction Giving from Application / Charging   <<<");
    LOG_INF("══════════════════════════════════════════════════════");

    /* Stage 1: Warm Amber / Yellow for 5.0 seconds */
    aed_led_set_mode(AED_LED_MODE_SHOCK_INSTRUCTION);

    /* Wait 5 seconds (5000ms) for instruction giving before Stage 2 (Red) */
    k_work_reschedule(&s_shock_seq_work, K_MSEC(TIMING_SHOCK_INSTRUCTION_MS));
}

/* ─────────────────────────────────────────────────────────────────────────── *
 * shock_sequence_work_handler()
 * Stage 1 (Alert Burst) → Stage 2 (Delivering Red 5s) → Stage 3 (Green Latch 1s)
 * ─────────────────────────────────────────────────────────────────────────── */
static void shock_sequence_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (s_shock_stage == 1) {
        /* Stage 1 (Alert Burst) complete → Stage 2 (Delivering Red 5s) */
        s_shock_stage = 2;
        s_aed_state   = AED_STATE_SHOCK;

        LOG_INF("══════════════════════════════════════════════════════");
        LOG_INF(">>> [MODE 04: SHOCK GIVING] URGENT RED 5 Hz HAZARD <<<");
        LOG_INF(">>> Firing 1ms Sync Pulse on GPIO 7                <<<");
        LOG_INF("══════════════════════════════════════════════════════");

        /* 1. Visual: Urgent Red rapid hazard strobe (5 Hz / 100ms ON / 100ms OFF) */
        aed_led_set_mode(AED_LED_MODE_SHOCK_GIVING);

        /* 2. Fire 1ms hardware sync pulse on GPIO 7 */
        aed_button_fire_sync_pulse(TIMING_SYNC_PULSE_MS * 1000U);

        /* Schedule Stage 3 in 5000 ms */
        k_work_reschedule(&s_shock_seq_work, K_MSEC(TIMING_SHOCK_GIVING_MS));
    }
    else if (s_shock_stage == 2) {
        /* Stage 2 (Red Hazard 5s) complete → Stage 3 (Green Latch 1s) */
        s_shock_stage = 0;
        s_aed_state   = AED_STATE_DISARMED;

        LOG_INF("══════════════════════════════════════════════════════");
        LOG_INF(">>> [MODE 05: SHOCK OFF] PURE GREEN 1s CONFIRMATION <<<");
        LOG_INF(">>> Energy Safely Discharged — Resume CPR           <<<");
        LOG_INF("══════════════════════════════════════════════════════");

        /* 1. Visual: Pure Green 1.0s confirmation latch → then returns to Mode 02 */
        aed_led_set_mode(AED_LED_MODE_SHOCK_OFF_LATCH);

        /* Restore BLE advertisement state to PAIRED (Manikin still connected) */
        aed_ble_update_adv_state(AED_BLE_STATE_PAIRED);
    }
}

/* ─────────────────────────────────────────────────────────────────────────── *
 * BLE CONNECTION CALLBACKS (from aed_ble.c)
 * ─────────────────────────────────────────────────────────────────────────── */

/** Manikin connected → Mode 02 (Solid Green) */
static void on_manikin_connected(bool connected)
{
    if (connected) {
        LOG_INF(">>> [AED] Manikin BLE connected — switching to Mode 02 (Solid Green) <<<");
        aed_ble_update_adv_state(AED_BLE_STATE_PAIRED);
        aed_led_set_mode(AED_LED_MODE_PEER_ACTIVE);
    } else {
        LOG_INF("<<< [AED] Manikin BLE disconnected — returning to Mode 01 (Blue Breathing) >>>");
        aed_ble_update_adv_state(AED_BLE_STATE_AVAILABLE);
        /* Cancel any active shock sequence */
        k_work_cancel_delayable(&s_shock_seq_work);
        s_shock_stage = 0;
        s_aed_state   = AED_STATE_DISARMED;
        aed_led_set_mode(AED_LED_MODE_WAITING_FOR_PEER);
    }
}

/** Manikin sent SHOCK_ON (arm=true) or SHOCK_OFF (arm=false) via BLE */
static void on_shock_cmd_from_manikin(bool arm)
{
    if (arm) {
        LOG_INF(">>> [AED] BLE SHOCK_ON received from Manikin — Initiating Shock Sequence <<<");
        start_shock_sequence();
    } else {
        LOG_INF(">>> [AED] BLE SHOCK_OFF received from Manikin (sequence auto-completes) <<<");
        /* Shock sequence auto-completes — SHOCK_OFF is informational */
    }
}

/** Manikin sent AED_ON / AED_OFF command */
static void on_aed_power_cmd(bool on)
{
    s_aed_active = on;
    if (on) {
        LOG_INF(">>> AED CONTROLLER: POWER ON (via BLE command) <<<");
        if (aed_ble_is_connected()) {
            aed_led_set_mode(AED_LED_MODE_PEER_ACTIVE);
        } else {
            aed_led_set_mode(AED_LED_MODE_WAITING_FOR_PEER);
        }
    } else {
        LOG_INF("<<< AED CONTROLLER: POWER OFF (via BLE command) >>>");
        k_work_cancel_delayable(&s_shock_seq_work);
        s_shock_stage = 0;
        s_aed_state   = AED_STATE_DISARMED;
        aed_led_set_mode(AED_LED_MODE_OFF);
    }
}

/* ─────────────────────────────────────────────────────────────────────────── *
 * BUTTON CALLBACKS
 * ─────────────────────────────────────────────────────────────────────────── */

/** Fired when physical shock button (GPIO 9) is pressed */
static void on_shock_button_pressed(void)
{
    LOG_INF(">>> AED SHOCK BUTTON PRESSED (GPIO 9) — Initiating Shock Sequence <<<");
    start_shock_sequence();
}

/** Fired when power switch (GPIO 6) toggles */
static void on_aed_power_switch_toggled(bool is_switch_on)
{
    on_aed_power_cmd(is_switch_on);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 * MAIN
 * ─────────────────────────────────────────────────────────────────────────── */
int main(void)
{
    LOG_INF("══════════════════════════════════════════════════════");
    LOG_INF(" CPR External AED Unit v%s [Zephyr RTOS]", AED_FW_VERSION_STR);
    LOG_INF(" Target  : ESP32-C3 Super Mini");
    LOG_INF(" Protocol: BLE PERIPHERAL (Manikin connects to AED)");
    LOG_INF(" Device  : %s", AED_BLE_DEVICE_NAME);
    LOG_INF(" Red: GPIO 3 | Grn: GPIO 4 | Blu: GPIO 5 | Sync: GPIO 7");
    LOG_INF("══════════════════════════════════════════════════════");

    int ret;

    /* 1. Initialize LED subsystem (starts in Mode 01: Deep Blue Breathing) */
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

    /* 4. Read physical power switch state at boot */
    s_aed_active = aed_power_switch_is_on();
    if (!s_aed_active) {
        LOG_INF("AED Controller started in POWER OFF state");
        aed_led_set_mode(AED_LED_MODE_OFF);
    }

    /* 5. Initialize BLE subsystem
     *    - Enables BLE stack
     *    - Registers NUS GATT service (Manikin writes SHOCK_ON/OFF here)
     *    - Starts advertising as "AED_001"
     *    - App scans and sees "AED_001" but NEVER connects to AED directly
     *    - App assigns AED to Manikin; Manikin connects to AED as Central
     */
    ret = aed_ble_init(on_shock_cmd_from_manikin,
                       on_manikin_connected,
                       on_aed_power_cmd);
    if (ret) {
        LOG_ERR("BLE init failed (err %d) — running without wireless", ret);
    }

    /* 6. If active on boot, show Blue Breathing (Mode 01) while waiting for Manikin */
    if (s_aed_active) {
        aed_led_set_mode(AED_LED_MODE_WAITING_FOR_PEER);
        LOG_INF("══════════════════════════════════════════════════════");
        LOG_INF(">>> AED POWERED ON: DEEP BLUE BREATHING RHYTHM     <<<");
        LOG_INF(">>> Advertising as '%s' — waiting for Manikin  <<<", AED_BLE_DEVICE_NAME);
        LOG_INF(">>> Press Shock Button (GPIO 9) to Deliver Shock    <<<");
        LOG_INF("══════════════════════════════════════════════════════");
    }

    LOG_INF("AED System Ready.");

    /* 7. Heartbeat loop */
    while (1) {
        const char *led_mode_str = "UNKNOWN";
        switch (aed_led_get_mode()) {
            case AED_LED_MODE_OFF:                led_mode_str = "OFF";                        break;
            case AED_LED_MODE_WAITING_FOR_PEER:   led_mode_str = "MODE 01: WAITING (BLUE)";    break;
            case AED_LED_MODE_PEER_ACTIVE:        led_mode_str = "MODE 02: PEER ACTIVE (GREEN)";break;
            case AED_LED_MODE_SHOCK_ON_ALERT:     led_mode_str = "MODE 03: ALERT (AMBER)";     break;
            case AED_LED_MODE_SHOCK_GIVING:       led_mode_str = "MODE 04: SHOCK (RED 5Hz)";   break;
            case AED_LED_MODE_SHOCK_OFF_LATCH:    led_mode_str = "MODE 05: COMPLETE (GREEN)";  break;
        }

        LOG_INF("[AED HEALTH] State: %s | LED: %s | Active: %s | BLE: %s",
                (s_aed_state == AED_STATE_ARMED) ? "ARMED"   :
                (s_aed_state == AED_STATE_SHOCK)  ? "SHOCK"   : "STANDBY",
                led_mode_str,
                s_aed_active              ? "YES"       : "NO",
                aed_ble_is_connected()    ? "MANIKIN CONNECTED" : "ADVERTISING");

        k_msleep(5000);
    }

    return 0;
}
