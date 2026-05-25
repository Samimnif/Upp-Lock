/*
 * RGB LED Driver for Zephyr RTOS
 *
 * Controls a common-cathode RGB LED via three GPIOs (R, G, B).
 * Implements the Zephyr LED API (drivers/led.h).
 *
 * DTS compatible: "custom,rgb-led"
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rgb_led, CONFIG_LED_LOG_LEVEL);

#define DT_DRV_COMPAT custom_rgb_led

/* --------------------------------------------------------------------------
 * Driver config & data structs
 * -------------------------------------------------------------------------- */

struct rgb_led_config {
    struct gpio_dt_spec gpio_r;
    struct gpio_dt_spec gpio_g;
    struct gpio_dt_spec gpio_b;
};

struct rgb_led_data {
    bool r_on;
    bool g_on;
    bool b_on;
};

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

static int set_channel(const struct device *dev, uint32_t channel, bool on)
{
    const struct rgb_led_config *cfg = dev->config;
    struct rgb_led_data       *data = dev->data;
    const struct gpio_dt_spec *pin;

    switch (channel) {
    case 0: pin = &cfg->gpio_r; data->r_on = on; break;
    case 1: pin = &cfg->gpio_g; data->g_on = on; break;
    case 2: pin = &cfg->gpio_b; data->b_on = on; break;
    default:
        LOG_ERR("Invalid channel %u (0=R, 1=G, 2=B)", channel);
        return -EINVAL;
    }

    return gpio_pin_set_dt(pin, on ? 1 : 0);
}

/* --------------------------------------------------------------------------
 * Zephyr LED API implementation
 * -------------------------------------------------------------------------- */

static int rgb_led_on(const struct device *dev, uint32_t channel)
{
    return set_channel(dev, channel, true);
}

static int rgb_led_off(const struct device *dev, uint32_t channel)
{
    return set_channel(dev, channel, false);
}

/*
 * set_brightness: maps 0–255 brightness to on/off for each channel.
 * Channel encodes which colour: 0=R, 1=G, 2=B.
 * Brightness 0 → off, anything else → on (binary control; no PWM).
 */
static int rgb_led_set_brightness(const struct device *dev,
                                  uint32_t channel, uint8_t brightness)
{
    return set_channel(dev, channel, brightness > 0);
}

static const struct led_driver_api rgb_led_api = {
    .on             = rgb_led_on,
    .off            = rgb_led_off,
    .set_brightness = rgb_led_set_brightness,
};

/* --------------------------------------------------------------------------
 * Initialisation
 * -------------------------------------------------------------------------- */

static int rgb_led_init(const struct device *dev)
{
    const struct rgb_led_config *cfg = dev->config;
    int ret;

    /* Configure R pin */
    if (!gpio_is_ready_dt(&cfg->gpio_r)) {
        LOG_ERR("GPIO R (pin %d) not ready", cfg->gpio_r.pin);
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&cfg->gpio_r, GPIO_OUTPUT_INACTIVE);
    if (ret) {
        LOG_ERR("Failed to configure R pin: %d", ret);
        return ret;
    }

    /* Configure G pin */
    if (!gpio_is_ready_dt(&cfg->gpio_g)) {
        LOG_ERR("GPIO G (pin %d) not ready", cfg->gpio_g.pin);
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&cfg->gpio_g, GPIO_OUTPUT_INACTIVE);
    if (ret) {
        LOG_ERR("Failed to configure G pin: %d", ret);
        return ret;
    }

    /* Configure B pin (GPIO 28) */
    if (!gpio_is_ready_dt(&cfg->gpio_b)) {
        LOG_ERR("GPIO B (pin %d) not ready", cfg->gpio_b.pin);
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&cfg->gpio_b, GPIO_OUTPUT_INACTIVE);
    if (ret) {
        LOG_ERR("Failed to configure B pin: %d", ret);
        return ret;
    }

    LOG_INF("RGB LED driver initialised (R=%d G=%d B=%d)",
            cfg->gpio_r.pin, cfg->gpio_g.pin, cfg->gpio_b.pin);
    return 0;
}

/* --------------------------------------------------------------------------
 * Device instantiation macro
 * -------------------------------------------------------------------------- */

#define RGB_LED_INIT(inst)                                                   \
                                                                              \
static const struct rgb_led_config rgb_led_config_##inst = {                  \
    .gpio_r = GPIO_DT_SPEC_INST_GET(inst, red_gpios),                         \
    .gpio_g = GPIO_DT_SPEC_INST_GET(inst, green_gpios),                       \
    .gpio_b = GPIO_DT_SPEC_INST_GET(inst, blue_gpios),                        \
};                                                                            \
                                                                              \
static struct rgb_led_data rgb_led_data_##inst;                               \
                                                                              \
DEVICE_DT_INST_DEFINE(inst,                                                   \
                      rgb_led_init,                                           \
                      NULL,                                                   \
                      &rgb_led_data_##inst,                                   \
                      &rgb_led_config_##inst,                                 \
                      POST_KERNEL,                                            \
                      CONFIG_LED_INIT_PRIORITY,                               \
                      &rgb_led_api);

DT_INST_FOREACH_STATUS_OKAY(RGB_LED_INIT)