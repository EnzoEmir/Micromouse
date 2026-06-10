#include "wifi.hpp"

static bool g_connected = false;
static int g_init_count = 0;

void wifi_init_sta(const char*, const char*) {
    g_init_count++;
    g_connected = true;  // the real blocking call returns once connected
}
bool wifi_is_connected(void) { return g_connected; }

void mock_wifi_set_connected(bool connected) { g_connected = connected; }
int mock_wifi_init_count(void) { return g_init_count; }
void mock_wifi_reset(void) {
    g_connected = false;
    g_init_count = 0;
}
