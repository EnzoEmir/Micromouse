// Host mock of driver/gpio.h.
#pragma once

#include <cstdint>

#include "esp_err.h"

// On the ESP32 gpio_num_t is an enum; the firmware casts ints to it, so a plain
// integer type is sufficient for the host build.
typedef int gpio_num_t;

// GPIO_NUM_0 .. GPIO_NUM_39 map to their integer index.
enum {
    GPIO_NUM_0 = 0, GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_4,
    GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_8, GPIO_NUM_9,
    GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14,
    GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19,
    GPIO_NUM_20, GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_24,
    GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_28, GPIO_NUM_29,
    GPIO_NUM_30, GPIO_NUM_31, GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_34,
    GPIO_NUM_35, GPIO_NUM_36, GPIO_NUM_37, GPIO_NUM_38, GPIO_NUM_39,
};

typedef enum {
    GPIO_MODE_DISABLE = 0,
    GPIO_MODE_INPUT,
    GPIO_MODE_OUTPUT,
    GPIO_MODE_OUTPUT_OD,
    GPIO_MODE_INPUT_OUTPUT,
} gpio_mode_t;

#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_INTR_DISABLE 0

typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gpio_config(const gpio_config_t* cfg);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);

// Test helper: last level driven on a pin (or -1 if untouched).
int mock_gpio_level(int pin);
void mock_gpio_reset(void);

#ifdef __cplusplus
}
#endif
