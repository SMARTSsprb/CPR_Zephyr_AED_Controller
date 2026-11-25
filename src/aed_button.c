/**
 * @file    aed_button.c
 * @brief   AED Shock & Power ON/OFF Button Drivers (Zephyr RTOS)
 * @version 2.0.0
 */

#include "aed_button.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(aed_button, LOG_LEVEL_INF);

/* Devicetree specifications */
static const struct gpio_dt_spec s_shock_btn_spec = GPIO_DT_SPEC_GET_OR(DT_ALIAS(aed_shock_btn), gpios, {0});
static const struct gpio_dt_spec s_power_btn_spec = GPIO_DT_SPEC_GET_OR(DT_ALIAS(aed_power_btn), gpios, {0});
static const struct gpio_dt_spec s_sync_spec      = GPIO_DT_SPEC_GET_OR(DT_ALIAS(aed_sync_out), gpios, {0});

static const struct device *s_gpio_dev = NULL;

static struct gpio_callback s_shock_cb_data;
static struct gpio_callback s_power_cb_data;

static aed_shock_btn_cb_t s_shock_cb = NULL;
static aed_power_btn_cb_t s_power_cb = NULL;

static int64_t s_last_shock_ms = 0;
static int64_t s_last_power_ms = 0;

void aed_button_fire_sync_pulse(uint32_t duration_us)
{
    if (s_sync_spec.port && device_is_ready(s_sync_spec.port)) {
        gpio_pin_set_dt(&s_sync_spec, 1);
        k_busy_wait(duration_us);
        gpio_pin_set_dt(&s_sync_spec, 0);
    } else if (s_gpio_dev) {
        gpio_pin_set_raw(s_gpio_dev, HAL_PIN_SYNC_OUT, 1);
        k_busy_wait(duration_us);
        gpio_pin_set_raw(s_gpio_dev, HAL_PIN_SYNC_OUT, 0);
    }
}

/* Shock Button Interrupt Handler */
static void shock_btn_interrupt_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    /* Debounce only. Whether a press is allowed is decided by the AED state
     * machine in main.c (shock button works only in the "waiting for press" state). */
    int64_t now = k_uptime_get();
    if ((now - s_last_shock_ms) < TIMING_BTN_DEBOUNCE_MS) {
        return;
    }

    s_last_shock_ms = now;

    if (s_shock_cb) {
        s_shock_cb();
    }
}

/* Power button (momentary) Interrupt Handler - press edge only */
static void power_btn_interrupt_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    int64_t now = k_uptime_get();
    if ((now - s_last_power_ms) < TIMING_BTN_DEBOUNCE_MS) {
        return;
    }

    s_last_power_ms = now;

    if (s_power_cb) {
        s_power_cb();
    }
}

int aed_button_init(aed_shock_btn_cb_t shock_cb, aed_power_btn_cb_t power_cb)
{
    s_shock_cb = shock_cb;
    s_power_cb = power_cb;

    /* 1. Configure Hardware Sync Pulse Output (GPIO 7) */
    if (s_sync_spec.port && device_is_ready(s_sync_spec.port)) {
        gpio_pin_configure_dt(&s_sync_spec, GPIO_OUTPUT_INACTIVE);
        LOG_INF("Sync output configured via DT alias on GPIO %d", s_sync_spec.pin);
    } else {
        s_gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
        if (device_is_ready(s_gpio_dev)) {
            gpio_pin_configure(s_gpio_dev, HAL_PIN_SYNC_OUT, GPIO_OUTPUT_INACTIVE);
            LOG_INF("Sync output fallback initialized on GPIO %d", HAL_PIN_SYNC_OUT);
        }
    }

    /* 2. Configure Shock Button Input with Interrupt (GPIO 9) */
    if (s_shock_btn_spec.port && device_is_ready(s_shock_btn_spec.port)) {
        int ret = gpio_pin_configure_dt(&s_shock_btn_spec, GPIO_INPUT | GPIO_PULL_UP);
        if (ret < 0) {
            LOG_ERR("Failed to configure shock button DT (err %d)", ret);
            return ret;
        }

        ret = gpio_pin_interrupt_configure_dt(&s_shock_btn_spec, GPIO_INT_EDGE_TO_ACTIVE);
        if (ret < 0) {
            LOG_ERR("Failed to configure shock interrupt DT (err %d)", ret);
            return ret;
        }

        gpio_init_callback(&s_shock_cb_data, shock_btn_interrupt_handler, BIT(s_shock_btn_spec.pin));
        gpio_add_callback(s_shock_btn_spec.port, &s_shock_cb_data);
        LOG_INF("Shock button configured on DT pin %d (Active-LOW)", s_shock_btn_spec.pin);
    } else {
        if (!s_gpio_dev) {
            s_gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
        }
        if (s_gpio_dev && device_is_ready(s_gpio_dev)) {
            gpio_pin_configure(s_gpio_dev, HAL_PIN_SHOCK_BUTTON, GPIO_INPUT | GPIO_PULL_UP);
            gpio_pin_interrupt_configure(s_gpio_dev, HAL_PIN_SHOCK_BUTTON, GPIO_INT_EDGE_FALLING);

            gpio_init_callback(&s_shock_cb_data, shock_btn_interrupt_handler, BIT(HAL_PIN_SHOCK_BUTTON));
            gpio_add_callback(s_gpio_dev, &s_shock_cb_data);
            LOG_INF("Shock button configured on fallback GPIO %d", HAL_PIN_SHOCK_BUTTON);
        }
    }

    /* 3. Configure Power ON/OFF Latching Switch Input with BOTH Edges Interrupt (GPIO 6) */
    if (s_power_btn_spec.port && device_is_ready(s_power_btn_spec.port)) {
        int ret = gpio_pin_configure_dt(&s_power_btn_spec, GPIO_INPUT | GPIO_PULL_UP);
        if (ret < 0) {
            LOG_ERR("Failed to configure power switch DT (err %d)", ret);
            return ret;
        }

        ret = gpio_pin_interrupt_configure_dt(&s_power_btn_spec, GPIO_INT_EDGE_TO_ACTIVE);
        if (ret < 0) {
            LOG_ERR("Failed to configure power button interrupt DT (err %d)", ret);
            return ret;
        }

        gpio_init_callback(&s_power_cb_data, power_btn_interrupt_handler, BIT(s_power_btn_spec.pin));
        gpio_add_callback(s_power_btn_spec.port, &s_power_cb_data);
        LOG_INF("Power button configured on DT pin %d (press edge)", s_power_btn_spec.pin);
    } else {
        if (!s_gpio_dev) {
            s_gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
        }
        if (s_gpio_dev && device_is_ready(s_gpio_dev)) {
            gpio_pin_configure(s_gpio_dev, HAL_PIN_POWER_BUTTON, GPIO_INPUT | GPIO_PULL_UP);
            gpio_pin_interrupt_configure(s_gpio_dev, HAL_PIN_POWER_BUTTON, GPIO_INT_EDGE_FALLING);

            gpio_init_callback(&s_power_cb_data, power_btn_interrupt_handler, BIT(HAL_PIN_POWER_BUTTON));
            gpio_add_callback(s_gpio_dev, &s_power_cb_data);
            LOG_INF("Power button configured on fallback GPIO %d (press edge)", HAL_PIN_POWER_BUTTON);
        }
    }

    return 0;
}

bool aed_power_switch_is_on(void)
{
#if AED_POWER_SWITCH_HARDWARE_CUT
    /* Option 1: Direct in-line battery switch. Board is running = switch is ON */
    return true;
#else
    if (s_power_btn_spec.port && device_is_ready(s_power_btn_spec.port)) {
        /* Active-LOW: Switch closed to GND = 0 (logic level LOW = ON) */
        return gpio_pin_get_dt(&s_power_btn_spec) == 0;
    } else if (s_gpio_dev) {
        return gpio_pin_get_raw(s_gpio_dev, HAL_PIN_POWER_BUTTON) == 0;
    }
    return true; /* Default ON if pin not defined/unconnected */
#endif
}

bool aed_button_is_pressed(void)
{
    if (s_shock_btn_spec.port && device_is_ready(s_shock_btn_spec.port)) {
        return gpio_pin_get_dt(&s_shock_btn_spec) > 0;
    } else if (s_gpio_dev) {
        return gpio_pin_get_raw(s_gpio_dev, HAL_PIN_SHOCK_BUTTON) == 0;
    }
    return false;
}
