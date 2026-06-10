// Shared implementation of the gpio/ledc/pcnt host mocks, with test-inspectable
// state.
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"

// ---- GPIO ------------------------------------------------------------------
static int g_gpio_level[64];
static bool g_gpio_init = false;

static void gpio_ensure_init() {
    if (g_gpio_init) return;
    for (int i = 0; i < 64; ++i) g_gpio_level[i] = -1;
    g_gpio_init = true;
}

extern "C" esp_err_t gpio_config(const gpio_config_t*) { return ESP_OK; }

extern "C" esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) {
    gpio_ensure_init();
    if (pin >= 0 && pin < 64) g_gpio_level[pin] = (int)level;
    return ESP_OK;
}

extern "C" int mock_gpio_level(int pin) {
    gpio_ensure_init();
    if (pin >= 0 && pin < 64) return g_gpio_level[pin];
    return -1;
}

extern "C" void mock_gpio_reset(void) {
    g_gpio_init = false;
    gpio_ensure_init();
}

// ---- LEDC ------------------------------------------------------------------
static int g_ledc_set_duty[LEDC_CHANNEL_MAX];
static int g_ledc_live_duty[LEDC_CHANNEL_MAX];  // visible only after update_duty
static bool g_ledc_init = false;

static void ledc_ensure_init() {
    if (g_ledc_init) return;
    for (int i = 0; i < LEDC_CHANNEL_MAX; ++i) {
        g_ledc_set_duty[i] = -1;
        g_ledc_live_duty[i] = -1;
    }
    g_ledc_init = true;
}

extern "C" esp_err_t ledc_channel_config(const ledc_channel_config_t*) { return ESP_OK; }

extern "C" esp_err_t ledc_set_duty(int, int channel, uint32_t duty) {
    ledc_ensure_init();
    if (channel >= 0 && channel < LEDC_CHANNEL_MAX) g_ledc_set_duty[channel] = (int)duty;
    return ESP_OK;
}

extern "C" esp_err_t ledc_update_duty(int, int channel) {
    ledc_ensure_init();
    if (channel >= 0 && channel < LEDC_CHANNEL_MAX)
        g_ledc_live_duty[channel] = g_ledc_set_duty[channel];
    return ESP_OK;
}

extern "C" int mock_ledc_duty(int channel) {
    ledc_ensure_init();
    if (channel >= 0 && channel < LEDC_CHANNEL_MAX) return g_ledc_live_duty[channel];
    return -1;
}

extern "C" void mock_ledc_reset(void) {
    g_ledc_init = false;
    ledc_ensure_init();
}

// ---- PCNT ------------------------------------------------------------------
static int g_pcnt_count = 0;
static int g_pcnt_unit_token = 1;
static int g_pcnt_chan_token = 1;

extern "C" esp_err_t pcnt_new_unit(const pcnt_unit_config_t*, pcnt_unit_handle_t* ret_unit) {
    if (ret_unit) *ret_unit = &g_pcnt_unit_token;
    return ESP_OK;
}
extern "C" esp_err_t pcnt_unit_set_glitch_filter(pcnt_unit_handle_t,
                                                 const pcnt_glitch_filter_config_t*) {
    return ESP_OK;
}
extern "C" esp_err_t pcnt_new_channel(pcnt_unit_handle_t, const pcnt_chan_config_t*,
                                      pcnt_channel_handle_t* ret_chan) {
    if (ret_chan) *ret_chan = &g_pcnt_chan_token;
    return ESP_OK;
}
extern "C" esp_err_t pcnt_channel_set_edge_action(pcnt_channel_handle_t, int, int) { return ESP_OK; }
extern "C" esp_err_t pcnt_channel_set_level_action(pcnt_channel_handle_t, int, int) { return ESP_OK; }
extern "C" esp_err_t pcnt_unit_enable(pcnt_unit_handle_t) { return ESP_OK; }
extern "C" esp_err_t pcnt_unit_disable(pcnt_unit_handle_t) { return ESP_OK; }
extern "C" esp_err_t pcnt_unit_clear_count(pcnt_unit_handle_t) {
    g_pcnt_count = 0;
    return ESP_OK;
}
extern "C" esp_err_t pcnt_unit_start(pcnt_unit_handle_t) { return ESP_OK; }
extern "C" esp_err_t pcnt_unit_stop(pcnt_unit_handle_t) { return ESP_OK; }
extern "C" esp_err_t pcnt_unit_get_count(pcnt_unit_handle_t, int* value) {
    if (value) *value = g_pcnt_count;
    return ESP_OK;
}

extern "C" void mock_pcnt_set_count(int value) { g_pcnt_count = value; }
extern "C" void mock_pcnt_reset(void) { g_pcnt_count = 0; }
