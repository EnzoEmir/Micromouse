// Host mock of driver/ledc.h.
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "driver/gpio.h"

typedef enum {
    LEDC_CHANNEL_0 = 0, LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3,
    LEDC_CHANNEL_4, LEDC_CHANNEL_5, LEDC_CHANNEL_6, LEDC_CHANNEL_7,
    LEDC_CHANNEL_MAX,
} ledc_channel_t;

typedef enum {
    LEDC_HIGH_SPEED_MODE = 0,
    LEDC_LOW_SPEED_MODE,
    LEDC_SPEED_MODE_MAX,
} ledc_mode_t;

typedef enum {
    LEDC_TIMER_0 = 0, LEDC_TIMER_1, LEDC_TIMER_2, LEDC_TIMER_3, LEDC_TIMER_MAX,
} ledc_timer_t;

#define LEDC_INTR_DISABLE 0

typedef struct {
    int speed_mode;
    int channel;
    int timer_sel;
    int intr_type;
    int gpio_num;
    int duty;
    int hpoint;
} ledc_channel_config_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ledc_channel_config(const ledc_channel_config_t* cfg);
esp_err_t ledc_set_duty(int speed_mode, int channel, uint32_t duty);
esp_err_t ledc_update_duty(int speed_mode, int channel);

// Test helper: last duty pushed to a channel (after update), or -1.
int mock_ledc_duty(int channel);
void mock_ledc_reset(void);

#ifdef __cplusplus
}
#endif
