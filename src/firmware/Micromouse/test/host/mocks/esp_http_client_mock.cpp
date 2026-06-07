#include "esp_http_client.h"

#include <cstring>
#include <string>

static std::string g_last_url;
static std::string g_last_body;
static int g_post_count = 0;
static esp_err_t g_perform_result = ESP_OK;
static int g_status_code = 200;
static int g_client_token = 1;

extern "C" esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config) {
    if (config && config->url) g_last_url = config->url;
    return &g_client_token;
}

extern "C" esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char*, const char*) {
    return ESP_OK;
}

extern "C" esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t, const char* data,
                                                    int len) {
    if (data && len >= 0)
        g_last_body.assign(data, (size_t)len);
    else if (data)
        g_last_body = data;
    return ESP_OK;
}

extern "C" esp_err_t esp_http_client_perform(esp_http_client_handle_t) {
    g_post_count++;
    return g_perform_result;
}

extern "C" int esp_http_client_get_status_code(esp_http_client_handle_t) { return g_status_code; }

extern "C" esp_err_t esp_http_client_cleanup(esp_http_client_handle_t) { return ESP_OK; }

extern "C" const char* mock_http_last_url(void) { return g_last_url.c_str(); }
extern "C" const char* mock_http_last_body(void) { return g_last_body.c_str(); }
extern "C" int mock_http_post_count(void) { return g_post_count; }
extern "C" void mock_http_reset(void) {
    g_last_url.clear();
    g_last_body.clear();
    g_post_count = 0;
    g_perform_result = ESP_OK;
    g_status_code = 200;
}
extern "C" void mock_http_set_perform_result(esp_err_t err) { g_perform_result = err; }
extern "C" void mock_http_set_status_code(int code) { g_status_code = code; }
