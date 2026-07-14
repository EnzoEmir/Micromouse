#include "telemetria.hpp"

#include <utility>

#include "esp_log.h"
#include "esp_timer.h"
#include "wifi.hpp"

Telemetria::Telemetria(std::string url, int64_t heartbeat_ms)
    : _server_url(std::move(url)),
      _inicio_ms(0),
      _ultimo_envio_ms(0),
      _timeout_heartbeat_ms(heartbeat_ms) {}

int64_t Telemetria::agora_ms() const {
    return esp_timer_get_time() / 1000;
}

int64_t Telemetria::timestamp_ms() const {
    return agora_ms() - _inicio_ms;
}

esp_err_t Telemetria::registrar(esp_err_t err) {
    if (err == ESP_OK) {
        _ultimo_envio_ms = agora_ms();
    }
    return err;
}

void Telemetria::inicializar(const char* ssid, const char* senha,
                             const Labirinto& labirinto, int bateria) {
    // Bloqueia até o Wi-Fi conectar (event group interno do módulo wifi).
    wifi_init_sta(ssid, senha);

    // Zera o relógio da corrida: o pacote inicial sai com timestamp_ms ~ 0.
    _inicio_ms = agora_ms();

    // Lado da largada derivado da celula de inicio: x==0 e o canto oeste
    // (esquerda); qualquer outro x e o canto leste (direita).
    const char* lado_largada =
        (labirinto.inicio().x == 0) ? "esquerda" : "direita";

    ESP_LOGI(TAG, "Wi-Fi pronto. Enviando configuracao inicial...");
    registrar(enviar_configuracao_inicial(_server_url.c_str(), timestamp_ms(),
                                          labirinto.tamanho(), lado_largada,
                                          bateria));
}

esp_err_t Telemetria::movimento(int x, int y, uint8_t paredes) {
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "Wi-Fi desconectado. Pacote de movimentacao nao enviado.");
        return ESP_FAIL;
    }
    return registrar(enviar_movimentacao(_server_url.c_str(), timestamp_ms(),
                                         x, y, paredes));
}

esp_err_t Telemetria::rota_otimizada(const Labirinto::Coordenada* rota, uint16_t n) {
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "Wi-Fi desconectado. Pacote de rota nao enviado.");
        return ESP_FAIL;
    }
    return registrar(enviar_rota_otimizada(_server_url.c_str(), timestamp_ms(), rota, n));
}

esp_err_t Telemetria::fim_corrida(bool sucesso, float v_med, int bateria) {
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "Wi-Fi desconectado. Pacote de fim de corrida nao enviado.");
        return ESP_FAIL;
    }
    return registrar(enviar_fim_corrida(_server_url.c_str(), timestamp_ms(),
                                        sucesso, v_med, bateria));
}

esp_err_t Telemetria::alerta_temperatura(float temp_c) {
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "Wi-Fi desconectado. Alerta de temperatura nao enviado.");
        return ESP_FAIL;
    }
    return registrar(enviar_alerta_temperatura(_server_url.c_str(), timestamp_ms(), temp_c));
}

void Telemetria::verificar_heartbeat(int bateria) {
    if (!wifi_is_connected()) {
        return;
    }
    const int64_t ocioso_ms = agora_ms() - _ultimo_envio_ms;
    if (ocioso_ms >= _timeout_heartbeat_ms) {
        ESP_LOGI(TAG, "Heartbeat: %lld ms sem envio.", ocioso_ms);
        registrar(enviar_heartbeat(_server_url.c_str(), timestamp_ms(), bateria));
    }
}
