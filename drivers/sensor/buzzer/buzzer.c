#include "buzzer.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_buzzer, CONFIG_LOG_DEFAULT_LEVEL);

#define DT_DRV_COMPAT custom_gpio_buzzer

struct buzzer_config {
    struct gpio_dt_spec gpio;
};

struct buzzer_data {
    bool active;
};

static int buzzer_init(const struct device *dev)
{
    const struct buzzer_config *cfg = dev->config;

    if (!gpio_is_ready_dt(&cfg->gpio)) {
        LOG_ERR("Buzzer GPIO not ready");
        return -ENODEV;
    }

    return gpio_pin_configure_dt(&cfg->gpio, GPIO_OUTPUT_INACTIVE);
}

int buzzer_on(const struct device *dev)
{
    const struct buzzer_config *cfg = dev->config;
    return gpio_pin_set_dt(&cfg->gpio, 1);
}

int buzzer_off(const struct device *dev)
{
    const struct buzzer_config *cfg = dev->config;
    return gpio_pin_set_dt(&cfg->gpio, 0);
}

int buzzer_beep(const struct device *dev, uint32_t freq_hz, uint32_t duration_ms)
{
    const struct buzzer_config *cfg = dev->config;

    if (freq_hz == 0 || duration_ms == 0) {
        return -EINVAL;
    }

    uint32_t half_period_us = 1000000U / (freq_hz * 2U);
    uint32_t cycles = (duration_ms * 1000U) / (half_period_us * 2U);

    for (uint32_t i = 0; i < cycles; i++) {
        gpio_pin_set_dt(&cfg->gpio, 1);
        k_busy_wait(half_period_us);
        gpio_pin_set_dt(&cfg->gpio, 0);
        k_busy_wait(half_period_us);
    }

    return gpio_pin_set_dt(&cfg->gpio, 0);
}

#define BUZZER_INIT(inst)                                                     \
                                                                              \
static const struct buzzer_config buzzer_config_##inst = {                    \
    .gpio = GPIO_DT_SPEC_INST_GET(inst, gpios),                               \
};                                                                            \
                                                                              \
static struct buzzer_data buzzer_data_##inst;                                 \
                                                                              \
DEVICE_DT_INST_DEFINE(inst,                                                   \
                      buzzer_init,                                            \
                      NULL,                                                   \
                      &buzzer_data_##inst,                                    \
                      &buzzer_config_##inst,                                  \
                      POST_KERNEL,                                            \
                      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,                     \
                      NULL);

DT_INST_FOREACH_STATUS_OKAY(BUZZER_INIT)