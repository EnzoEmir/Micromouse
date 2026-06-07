#include "esp_timer.h"

static int64_t g_time_us = 0;

extern "C" int64_t esp_timer_get_time(void) { return g_time_us; }

extern "C" void mock_timer_set_us(int64_t us) { g_time_us = us; }
extern "C" void mock_timer_advance_us(int64_t us) { g_time_us += us; }
extern "C" void mock_timer_reset(void) { g_time_us = 0; }
