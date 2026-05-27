#include "telemetria.hpp"

#include <utility>

#include "esp_log.h"
#include "esp_timer.h"
#include "wifi.hpp"

Telemetria::Telemetria(std::string url, int64_t heartbeat_ms)
    : _server_url(std::move(url)),
      _ultimo_envio_ms(0),
      _timeout_heartbeat_ms(heartbeat_ms) {}

int64_t Telemetria::agora_ms() const {
    return esp_timer_get_time() / 1000;
}

void Telemetria::inicializar(const char* ssid, const char* senha, const Labirinto& labirinto) {
    // Bloqueia até o Wi-Fi conectar (event group interno do módulo wifi).
    wifi_init_sta(ssid, senha);
    ESP_LOGI(TAG, "Wi-Fi pronto. Enviando pacote de inicio de mapeamento...");
    enviar(TipoEnvio::InicioMapeamento, labirinto, 0, "N", 0.0f, 0.0f);
}

esp_err_t Telemetria::enviar(TipoEnvio tipo,
                             const Labirinto& labirinto,
                             int soc,
                             const char* direcao,
                             float velocidade_media_cms,
                             float temperatura) {
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "Wi-Fi desconectado. Pacote '%s' nao enviado.",
                 tipo_envio_para_string(tipo));
        return ESP_FAIL;
    }

    DadosEnvio dados = {};
    dados.tipo = tipo;
    dados.velocidade_media_cms = velocidade_media_cms;
    dados.direcao = direcao;
    dados.temperatura = temperatura;
    dados.soc = soc;
    dados.timestamp_ms = agora_ms();

    esp_err_t err = enviar_dados_sensores(_server_url.c_str(), dados, labirinto);
    if (err == ESP_OK) {
        _ultimo_envio_ms = dados.timestamp_ms;
    }
    return err;
}

void Telemetria::verificar_heartbeat(const Labirinto& labirinto,
                                     int soc,
                                     const char* direcao,
                                     float temperatura) {
    const int64_t ocioso_ms = agora_ms() - _ultimo_envio_ms;
    if (ocioso_ms >= _timeout_heartbeat_ms) {
        ESP_LOGI(TAG, "Heartbeat: %lld ms sem envio.", ocioso_ms);
        enviar(TipoEnvio::Heartbeat, labirinto, soc, direcao, 0.0f, temperatura);
    }
}
