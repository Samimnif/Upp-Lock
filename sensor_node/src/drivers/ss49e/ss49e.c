#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(SS49E, LOG_LEVEL_INF);

struct ss49e_data {
    uint16_t raw_val;
};

static const struct device *adc_dev;

static struct adc_channel_cfg m_example_channel_cfg = {
    .gain = ADC_GAIN_1,         
    .reference = ADC_REF_INTERNAL, 
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = 0,
};

static int ss49e_sample_fetch(const struct device *dev, enum sensor_channel chan) {
    struct ss49e_data *data = dev->data;
    uint16_t buf;
    
    struct adc_sequence sequence = {
        .channels    = BIT(m_example_channel_cfg.channel_id),
        .buffer      = &buf,
        .buffer_size = sizeof(buf),
        .resolution  = 12,
    };

    if (!device_is_ready(adc_dev)) {
        return -ENODEV;
    }

    int err = adc_read(adc_dev, &sequence);
    if (err == 0) {
        data->raw_val = buf;
    }
    return err;
}

static int ss49e_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val) {
    struct ss49e_data *data = dev->data;
    float v = (data->raw_val / 4095.0f) * 3300.0f;
    float gauss = (v - 1650.0f) / 1.4f;

    val->val1 = (int32_t)gauss;
    val->val2 = (int32_t)((gauss - val->val1) * 1000000);
    return 0;
}

static const struct sensor_driver_api ss49e_api = {
    .sample_fetch = ss49e_sample_fetch,
    .channel_get = ss49e_channel_get,
};

static int ss49e_init(const struct device *dev) {
    adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));

    if (adc_dev == NULL) {
        adc_dev = device_get_binding("ADC");
    }

    if (!device_is_ready(adc_dev)) {
        LOG_ERR("ADC device not ready");
        return -ENODEV;
    }

    // 配置通道
    return adc_channel_setup(adc_dev, &m_example_channel_cfg);
}

static struct ss49e_data ss49e_data_0;

DEVICE_DEFINE(ss49e_dev, "SS49E", ss49e_init, NULL, 
              &ss49e_data_0, NULL, POST_KERNEL, 
              90, &ss49e_api);