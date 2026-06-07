// Host mock of esp_http_client.h. Captures the last POST so tests can assert on
// the serialized JSON payload without any network.
#pragma once

#include <cstddef>

#include "esp_err.h"

typedef void* esp_http_client_handle_t;

typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
} esp_http_client_method_t;

typedef struct {
    const char* url;
    int method;
    int timeout_ms;
} esp_http_client_config_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char* key,
                                     const char* value);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char* data,
                                         int len);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);

// Test helpers (not part of the real API).
const char* mock_http_last_url(void);     // last URL passed to init
const char* mock_http_last_body(void);    // last POST body
int mock_http_post_count(void);           // number of perform() calls
void mock_http_reset(void);
void mock_http_set_perform_result(esp_err_t err);  // what perform() returns
void mock_http_set_status_code(int code);

#ifdef __cplusplus
}
#endif
