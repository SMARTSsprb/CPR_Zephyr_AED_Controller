/**
 * @file    aed_led.c
 * @brief   AED Multi-Color RGB & Status Indicator LED Driver (Zephyr RTOS)
 * @version 2.1.0
 * 
 * Visual Feedback Modes, Color Codes & Waveforms:
 *  MODE 01: WAITING FOR PEER  - Deep Blue breathing (1.8s breath cycle, 5% - 100%)
 *  MODE 02: PEER ACTIVE&READY - Emerald Green continuous steady glow (100% Constant Solid ON)
 *  MODE 03: SHOCK ON (ALERT)  - Warm Amber/Yellow one-time triple strobe burst (3x 60ms ON / 60ms OFF)
 *  MODE 04: SHOCK GIVING(ARM) - Urgent Red rapid hazard strobe (5 Hz / 100ms ON / 100ms OFF)
 *  MODE 05: SHOCK OFF (DELIV) - Pure Green 1.0s confirmation latch -> returns to Peer Active Glow
 */

#include "aed_led.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(aed_led, LOG_LEVEL_INF);

/* Devicetree specifications */
static const struct gpio_dt_spec s_onboard_led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(aed_status_led), gpios, {0});
static const struct gpio_dt_spec s_rgb_r       = GPIO_DT_SPEC_GET_OR(DT_ALIAS(aed_rgb_red), gpios, {0});
static const struct gpio_dt_spec s_rgb_g       = GPIO_DT_SPEC_GET_OR(DT_ALIAS(aed_rgb_green), gpios, {0});
static const struct gpio_dt_spec s_rgb_b       = GPIO_DT_SPEC_GET_OR(DT_ALIAS(aed_rgb_blue), gpios, {0});
static const struct device *s_gpio_dev = NULL;

static volatile aed_led_mode_t s_led_mode = AED_LED_MODE_WAITING_FOR_PEER;
static struct k_work_delayable s_led_work;

/* High-resolution software PWM timer (1 ms tick, 50 Hz carrier frequency via 20 steps) */
static struct k_timer s_pwm_timer;
static volatile uint8_t s_duty_r = 0;   /* 0 - 100 % */
static volatile uint8_t s_duty_g = 0;   /* 0 - 100 % */
static volatile uint8_t s_duty_b = 0;   /* 0 - 100 % */
static volatile uint8_t s_pwm_cycle = 0;

/* Animation counters & state flags */
static uint16_t s_breath_step = 0;
static uint8_t  s_alert_step  = 0;
static bool     s_hazard_state = false;
static uint32_t s_latch_start_ms = 0;

/* MODE 01: Tuned 150-step Gamma-Corrected Sine/Cosine Breathing Table (3.0s cycle @ 20ms tick)
 * Reaches absolute true dark (0) at bottom, smooth organic inhale to 100%, and exhale back down */
static const uint8_t s_breath_table[TIMING_LED_BREATH_STEPS] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   2,
      2,   3,   3,   4,   5,   6,   7,   9,  10,  12,  14,  16,  18,  20,  22,
     25,  28,  30,  33,  36,  40,  43,  46,  49,  53,  56,  60,  63,  66,  70,
     73,  76,  79,  82,  85,  87,  89,  92,  93,  95,  97,  98,  99,  99, 100,
    100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,  99,  99,  98,
     97,  95,  93,  92,  89,  87,  85,  82,  79,  76,  73,  70,  66,  63,  60,
     56,  53,  49,  46,  43,  40,  36,  33,  30,  28,  25,  22,  20,  18,  16,
     14,  12,  10,   9,   7,   6,   5,   4,   3,   3,   2,   2,   1,   1,   1,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

/* Low-level hardware RGB output control */
static void hw_set_rgb(bool r, bool g, bool b)
{
#if RGB_LED_ACTIVE_LOW
    /* Common Anode polarity (0 = ON, 1 = OFF) */
    r = !r;
    g = !g;
    b = !b;
#endif

    if (s_rgb_r.port && device_is_ready(s_rgb_r.port)) {
        gpio_pin_set_dt(&s_rgb_r, r ? 1 : 0);
        gpio_pin_set_dt(&s_rgb_g, g ? 1 : 0);
        gpio_pin_set_dt(&s_rgb_b, b ? 1 : 0);
    } else if (s_gpio_dev) {
        gpio_pin_set_raw(s_gpio_dev, HAL_PIN_RGB_RED,   r ? 1 : 0);
        gpio_pin_set_raw(s_gpio_dev, HAL_PIN_RGB_GREEN, g ? 1 : 0);
        gpio_pin_set_raw(s_gpio_dev, HAL_PIN_RGB_BLUE,  b ? 1 : 0);
    }
}

/* Low-level hardware onboard LED output */
static void hw_set_onboard(bool on)
{
    if (s_onboard_led.port && device_is_ready(s_onboard_led.port)) {
        gpio_pin_set_dt(&s_onboard_led, on ? 1 : 0);
    } else if (s_gpio_dev) {
        /* Active-LOW on ESP32-C3 Super Mini onboard LED (0 = ON, 1 = OFF) */
        gpio_pin_set_raw(s_gpio_dev, HAL_PIN_STATUS_LED, on ? 0 : 1);
    }
}

/* 1 ms PWM Timer Handler for smooth flicker-free color mixing & breathing */
static void pwm_timer_handler(struct k_timer *dummy)
{
    ARG_UNUSED(dummy);

    /* 20-step PWM window (20 ms period = 50 Hz refresh rate) */
    s_pwm_cycle = (s_pwm_cycle + 1) % 20;
    uint8_t threshold = s_pwm_cycle * 5;

    bool r = (s_duty_r > threshold);
    bool g = (s_duty_g > threshold);
    bool b = (s_duty_b > threshold);

    hw_set_rgb(r, g, b);
}

/* Main state-machine & animation work handler */
static void led_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    switch (s_led_mode) {
        case AED_LED_MODE_OFF:
            s_duty_r = 0;
            s_duty_g = 0;
            s_duty_b = 0;
            hw_set_onboard(false);
            break;

        case AED_LED_MODE_WAITING_FOR_PEER: {
            /* MODE 01: Deep Blue Breathing Rhythm (1.8s breath cycle, 5% - 100%) */
            s_duty_r = 0;
            s_duty_g = 0;
            s_duty_b = s_breath_table[s_breath_step];

            s_breath_step = (s_breath_step + 1) % TIMING_LED_BREATH_STEPS;

            /* Synchronize onboard status LED with peak breath */
            hw_set_onboard(s_duty_b > 45);

            k_work_reschedule(&s_led_work, K_MSEC(TIMING_LED_BREATH_TICK_MS));
            break;
        }

        case AED_LED_MODE_PEER_ACTIVE:
            /* MODE 02: Emerald Green Continuous Steady Glow (100% Constant Solid ON) */
            s_duty_r = 0;
            s_duty_g = 100;
            s_duty_b = 0;
            hw_set_onboard(true);
            k_work_reschedule(&s_led_work, K_MSEC(500));
            break;

        case AED_LED_MODE_SHOCK_ON_ALERT:
            /* Stage 1: Warm Amber / Yellow for 5.0 seconds (Instruction giving from application / charging) */
            /* Vibrant Yellow: 100% Red + 80% Green, 0% Blue */
            s_duty_r = 100;
            s_duty_g = 80;
            s_duty_b = 0;
            hw_set_onboard(true);
            k_work_reschedule(&s_led_work, K_MSEC(500));
            break;

        case AED_LED_MODE_SHOCK_GIVING:
            /* Stage 2: Urgent Red for 5.0 seconds (Shock actively giving) */
            /* Urgent Red rapid hazard strobe (5 Hz / 100ms ON / 100ms OFF) */
            s_hazard_state = !s_hazard_state;
            if (s_hazard_state) {
                s_duty_r = 100;
                s_duty_g = 0;
                s_duty_b = 0;
                hw_set_onboard(true);
            } else {
                s_duty_r = 0;
                s_duty_g = 0;
                s_duty_b = 0;
                hw_set_onboard(false);
            }
            k_work_reschedule(&s_led_work, K_MSEC(TIMING_SHOCK_GIVING_STROBE_MS));
            break;

        case AED_LED_MODE_SHOCK_OFF_LATCH:
            /* Stage 3: Turns to Solid Green (Safe to touch patient / resume CPR) */
            s_duty_r = 0;
            s_duty_g = 100;
            s_duty_b = 0;
            hw_set_onboard(true);
            s_led_mode = AED_LED_MODE_PEER_ACTIVE;
            k_work_reschedule(&s_led_work, K_MSEC(500));
            break;

        default:
            break;
    }
}

int aed_led_init(void)
{
    /* 1. Configure Onboard Status LED */
    if (s_onboard_led.port && device_is_ready(s_onboard_led.port)) {
        gpio_pin_configure_dt(&s_onboard_led, GPIO_OUTPUT_INACTIVE);
        LOG_INF("Onboard Status LED configured via DT on GPIO %d", s_onboard_led.pin);
    } else {
        s_gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
        if (device_is_ready(s_gpio_dev)) {
            gpio_pin_configure(s_gpio_dev, HAL_PIN_STATUS_LED, GPIO_OUTPUT_INACTIVE);
            LOG_INF("Onboard Status LED configured via fallback on GPIO %d", HAL_PIN_STATUS_LED);
        }
    }

    /* 2. Configure RGB Channels */
    if (s_rgb_r.port && device_is_ready(s_rgb_r.port)) {
        gpio_pin_configure_dt(&s_rgb_r, GPIO_OUTPUT_INACTIVE);
        gpio_pin_configure_dt(&s_rgb_g, GPIO_OUTPUT_INACTIVE);
        gpio_pin_configure_dt(&s_rgb_b, GPIO_OUTPUT_INACTIVE);
        LOG_INF("RGB LED configured via DT (R:%d, G:%d, B:%d)",
                s_rgb_r.pin, s_rgb_g.pin, s_rgb_b.pin);
    } else {
        if (!s_gpio_dev) {
            s_gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
        }
        if (s_gpio_dev && device_is_ready(s_gpio_dev)) {
            gpio_pin_configure(s_gpio_dev, HAL_PIN_RGB_RED,   GPIO_OUTPUT_INACTIVE);
            gpio_pin_configure(s_gpio_dev, HAL_PIN_RGB_GREEN, GPIO_OUTPUT_INACTIVE);
            gpio_pin_configure(s_gpio_dev, HAL_PIN_RGB_BLUE,  GPIO_OUTPUT_INACTIVE);
            LOG_INF("RGB LED configured via fallback on GPIOs R:%d G:%d B:%d",
                    HAL_PIN_RGB_RED, HAL_PIN_RGB_GREEN, HAL_PIN_RGB_BLUE);
        } else {
            LOG_ERR("Failed to obtain GPIO device for RGB LED");
            return -ENODEV;
        }
    }

    /* Start with all LEDs off */
    hw_set_rgb(false, false, false);
    hw_set_onboard(false);

    /* 3. Initialize high-resolution 1ms PWM modulation timer */
    k_timer_init(&s_pwm_timer, pwm_timer_handler, NULL);
    k_timer_start(&s_pwm_timer, K_MSEC(1), K_MSEC(1));

    /* 4. Initialize animation & state work handler (starts in Mode 01: Deep Blue Breathing) */
    k_work_init_delayable(&s_led_work, led_work_handler);
    s_led_mode = AED_LED_MODE_WAITING_FOR_PEER;
    s_breath_step = 0;
    k_work_reschedule(&s_led_work, K_MSEC(10));

    LOG_INF("AED RGB LED subsystem initialized (MODE 01: WAITING FOR PEER / Deep Blue Breathing 1.8s)");
    return 0;
}

void aed_led_set_mode(aed_led_mode_t mode)
{
    s_led_mode = mode;

    switch (mode) {
        case AED_LED_MODE_WAITING_FOR_PEER:
            s_breath_step = 0;
            break;
        case AED_LED_MODE_SHOCK_ON_ALERT:
            s_alert_step = 0;
            break;
        case AED_LED_MODE_SHOCK_GIVING:
            s_hazard_state = false;
            break;
        case AED_LED_MODE_SHOCK_OFF_LATCH:
            s_latch_start_ms = k_uptime_get_32();
            break;
        default:
            break;
    }

    k_work_reschedule(&s_led_work, K_NO_WAIT);
}

aed_led_mode_t aed_led_get_mode(void)
{
    return s_led_mode;
}

void aed_led_trigger_waiting_for_peer(void)
{
    aed_led_set_mode(AED_LED_MODE_WAITING_FOR_PEER);
}

void aed_led_trigger_peer_active(void)
{
    aed_led_set_mode(AED_LED_MODE_PEER_ACTIVE);
}

void aed_led_trigger_shock_on_alert(void)
{
    aed_led_set_mode(AED_LED_MODE_SHOCK_ON_ALERT);
}

void aed_led_trigger_shock_giving(void)
{
    aed_led_set_mode(AED_LED_MODE_SHOCK_GIVING);
}

void aed_led_trigger_shock_off(void)
{
    aed_led_set_mode(AED_LED_MODE_SHOCK_OFF_LATCH);
}

void aed_led_trigger_shock_flash(void)
{
    aed_led_trigger_shock_off();
}
