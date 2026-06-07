// Host mock of driver/pulse_cnt.h (PCNT).
#pragma once

#include <cstdint>

#include "esp_err.h"

typedef void* pcnt_unit_handle_t;
typedef void* pcnt_channel_handle_t;

typedef struct {
    int high_limit;
    int low_limit;
} pcnt_unit_config_t;

typedef struct {
    uint32_t max_glitch_ns;
} pcnt_glitch_filter_config_t;

typedef struct {
    int edge_gpio_num;
    int level_gpio_num;
} pcnt_chan_config_t;

typedef enum {
    PCNT_CHANNEL_EDGE_ACTION_HOLD = 0,
    PCNT_CHANNEL_EDGE_ACTION_INCREASE,
    PCNT_CHANNEL_EDGE_ACTION_DECREASE,
} pcnt_channel_edge_action_t;

typedef enum {
    PCNT_CHANNEL_LEVEL_ACTION_KEEP = 0,
    PCNT_CHANNEL_LEVEL_ACTION_INVERSE,
    PCNT_CHANNEL_LEVEL_ACTION_HOLD,
} pcnt_channel_level_action_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pcnt_new_unit(const pcnt_unit_config_t* config, pcnt_unit_handle_t* ret_unit);
esp_err_t pcnt_unit_set_glitch_filter(pcnt_unit_handle_t unit,
                                      const pcnt_glitch_filter_config_t* config);
esp_err_t pcnt_new_channel(pcnt_unit_handle_t unit, const pcnt_chan_config_t* config,
                           pcnt_channel_handle_t* ret_chan);
esp_err_t pcnt_channel_set_edge_action(pcnt_channel_handle_t chan, int pos_act, int neg_act);
esp_err_t pcnt_channel_set_level_action(pcnt_channel_handle_t chan, int high_act, int low_act);
esp_err_t pcnt_unit_enable(pcnt_unit_handle_t unit);
esp_err_t pcnt_unit_disable(pcnt_unit_handle_t unit);
esp_err_t pcnt_unit_clear_count(pcnt_unit_handle_t unit);
esp_err_t pcnt_unit_start(pcnt_unit_handle_t unit);
esp_err_t pcnt_unit_stop(pcnt_unit_handle_t unit);
esp_err_t pcnt_unit_get_count(pcnt_unit_handle_t unit, int* value);

// Test helper: inject the value returned by pcnt_unit_get_count.
void mock_pcnt_set_count(int value);
void mock_pcnt_reset(void);

#ifdef __cplusplus
}
#endif
