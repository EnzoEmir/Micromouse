// Mock implementation of envio_dados for the telemetria unit tests. Records each
// call instead of performing any HTTP, and returns a configurable result.
#include "envio_dados.hpp"
#include "mock_envio_dados.h"

#include <cstring>

static EnvioLog g_log = {};
static esp_err_t g_result = ESP_OK;

static void record(int tipo, int64_t ts) {
    g_log.total_calls++;
    g_log.last_tipo = tipo;
    g_log.last_timestamp_ms = ts;
    if (tipo >= 0 && tipo < 8) g_log.count_by_tipo[tipo]++;
}

esp_err_t enviar_configuracao_inicial(const char*, int64_t ts, int dimensao, int bateria) {
    record(0, ts);
    g_log.last_dimensao = dimensao;
    g_log.last_bateria = bateria;
    return g_result;
}

esp_err_t enviar_movimentacao(const char*, int64_t ts, int x, int y, uint8_t w) {
    record(1, ts);
    g_log.last_x = x;
    g_log.last_y = y;
    g_log.last_w = w;
    return g_result;
}

esp_err_t enviar_rota_otimizada(const char*, int64_t ts, const Labirinto::Coordenada*,
                                uint16_t n) {
    record(2, ts);
    g_log.last_rota_n = n;
    return g_result;
}

esp_err_t enviar_fim_corrida(const char*, int64_t ts, bool sucesso, float v_med, int bateria) {
    record(3, ts);
    g_log.last_sucesso = sucesso;
    g_log.last_v_med = v_med;
    g_log.last_bateria = bateria;
    return g_result;
}

esp_err_t enviar_heartbeat(const char*, int64_t ts, int bateria) {
    record(4, ts);
    g_log.last_bateria = bateria;
    return g_result;
}

esp_err_t enviar_alerta_temperatura(const char*, int64_t ts, float temp_c) {
    record(5, ts);
    g_log.last_temp_c = temp_c;
    return g_result;
}

extern "C" const EnvioLog* mock_envio_get_log(void) { return &g_log; }
extern "C" void mock_envio_reset(void) {
    std::memset(&g_log, 0, sizeof(g_log));
    g_log.last_tipo = -1;
    g_result = ESP_OK;
}
extern "C" void mock_envio_set_result(esp_err_t err) { g_result = err; }
