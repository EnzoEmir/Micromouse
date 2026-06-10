// Host mock of the wifi module (overrides main/wifi/wifi.hpp on the test
// include path). Connection state is test-controlled.
#ifndef WIFI_HPP
#define WIFI_HPP

#include "esp_err.h"

void wifi_init_sta(const char* ssid, const char* password);
bool wifi_is_connected(void);

// Test helpers.
void mock_wifi_set_connected(bool connected);
int mock_wifi_init_count(void);
void mock_wifi_reset(void);

#endif
