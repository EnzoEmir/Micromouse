// Host mock of esp_timer.h — virtual clock controllable from tests.
#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Returns the virtual time in microseconds since "boot".
int64_t esp_timer_get_time(void);

// Test helpers (not part of the real ESP-IDF API).
void mock_timer_set_us(int64_t us);
void mock_timer_advance_us(int64_t us);
void mock_timer_reset(void);

#ifdef __cplusplus
}
#endif
