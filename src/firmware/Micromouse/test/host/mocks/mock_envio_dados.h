// Test-only inspection API for the envio_dados mock used by the telemetria
// tests. The actual enviar_*() functions are declared in the real
// envio_dados.hpp; this header exposes what the mock recorded.
#pragma once

#include <cstdint>

#include "esp_err.h"

struct EnvioLog {
    int total_calls;
    int last_tipo;             // -1 if none yet
    int64_t last_timestamp_ms;
    // Per-type call counters, indexed by TipoPacote value (0..5).
    int count_by_tipo[8];
    // Captured fields from the most recent call (union-ish, only relevant ones set).
    int last_dimensao;
    const char* last_lado_largada;  // config packet (tipo 0): "esquerda"/"direita"
    int last_bateria;
    int last_x, last_y;
    uint8_t last_w;
    bool last_sucesso;
    float last_v_med;
    float last_temp_c;
    uint16_t last_rota_n;
};

#ifdef __cplusplus
extern "C" {
#endif

const EnvioLog* mock_envio_get_log(void);
void mock_envio_reset(void);
void mock_envio_set_result(esp_err_t err);  // what every enviar_*() returns

#ifdef __cplusplus
}
#endif
