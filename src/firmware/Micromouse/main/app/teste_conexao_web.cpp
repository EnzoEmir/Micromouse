// Teste de conexao com o web: conecta no Wi-Fi e envia uma sequencia de pacotes de telemetria MOCKADOS (os 6 tipos de src/firmware/telemetria.md)

// idf.py flash monitor para testar

#include <cstdint>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "envio_dados.hpp"
#include "maze.hpp"
#include "wifi.hpp"

namespace {

static const char* TAG = "TESTE_WEB";

const char* WIFI_SSID   = "NOME_DO_SEU_WIFI";
const char* WIFI_PASS   = "SENHA_DO_SEU_WIFI";
const char* BACKEND_URL = "http://192.168.1.50:8000/api/telemetria";

// Pausa entre pacotes (ms), para o web ver os dados chegando ao longo do tempo.
constexpr int PAUSA_MS = 600;

int64_t s_inicio_ms = 0;  // referencia de tempo (instante de inicio da corrida)

// timestamp_ms relativo ao inicio da corrida, como pede a especificacao.
int64_t timestamp_ms() {
    return esp_timer_get_time() / 1000 - s_inicio_ms;
}

// Inicializa o NVS (requisito do esp_wifi_init), com o padrao de erase-e-retry.
void inicializar_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

// Envia os pacotes 0,1,2,3,5 simulando uma corrida 4x4 do (0,0) ao centro.
void enviar_sequencia_mock() {
    // Caminho mockado, com a bitmask de paredes por celula (N=1, S=2, L=4, O=8).
    struct CelulaMock { int x; int y; uint8_t w; };
    const CelulaMock caminho[] = {
        {0, 0, 10},  // Sul (2) + Oeste (8)
        {0, 1, 8},   // Oeste
        {0, 2, 9},   // Norte (1) + Oeste (8)
        {1, 2, 1},   // Norte
        {2, 2, 5},   // Norte (1) + Leste (4)
    };
    constexpr int n = sizeof(caminho) / sizeof(caminho[0]);

    // tipo 0 - Configuracao Inicial (labirinto 4x4, largada esquerda, bateria 100%).
    enviar_configuracao_inicial(BACKEND_URL, timestamp_ms(), 4, "esquerda", 100);
    vTaskDelay(pdMS_TO_TICKS(PAUSA_MS));

    // tipo 1 - Movimentacao / Descoberta de Paredes (uma por celula).
    for (int i = 0; i < n; ++i) {
        enviar_movimentacao(BACKEND_URL, timestamp_ms(),
                            caminho[i].x, caminho[i].y, caminho[i].w);
        vTaskDelay(pdMS_TO_TICKS(PAUSA_MS));
    }

    // tipo 2 - Rota Otimizada (mesmas coordenadas do caminho).
    Labirinto::Coordenada rota[n];
    for (int i = 0; i < n; ++i) {
        rota[i] = {static_cast<uint8_t>(caminho[i].x),
                   static_cast<uint8_t>(caminho[i].y)};
    }
    enviar_rota_otimizada(BACKEND_URL, timestamp_ms(), rota, n);
    vTaskDelay(pdMS_TO_TICKS(PAUSA_MS));

    // tipo 3 - Fim de Corrida (sucesso, ~0,22 m/s, bateria 88%).
    enviar_fim_corrida(BACKEND_URL, timestamp_ms(), true, 0.22f, 88);
    vTaskDelay(pdMS_TO_TICKS(PAUSA_MS));

    // tipo 5 - Alerta Critico de Temperatura (demonstra o alerta no web).
    enviar_alerta_temperatura(BACKEND_URL, timestamp_ms(), 61.0f);
    vTaskDelay(pdMS_TO_TICKS(PAUSA_MS));
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== Teste de conexao com o web (telemetria mockada) ===");

    inicializar_nvs();

    ESP_LOGI(TAG, "Conectando ao Wi-Fi '%s'...", WIFI_SSID);
    wifi_init_sta(WIFI_SSID, WIFI_PASS);
    if (!wifi_is_connected()) {
        ESP_LOGE(TAG, "Falha ao conectar no Wi-Fi. Verifique SSID/senha.");
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi conectado. Enviando mocks para %s", BACKEND_URL);

    s_inicio_ms = esp_timer_get_time() / 1000;

    // 1) Dispara a sequencia completa uma vez (tipos 0,1,2,3,5).
    enviar_sequencia_mock();

    // 2) Mantem a conexao viva com heartbeats (tipo 4) a cada 1,5 s. A bateria
    //    mockada cai aos poucos so para visualizar a variacao no web.
    ESP_LOGI(TAG, "Sequencia enviada. Mantendo heartbeats (tipo 4)...");
    int bateria = 88;
    while (true) {
        enviar_heartbeat(BACKEND_URL, timestamp_ms(), bateria);
        if (bateria > 0) --bateria;
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
