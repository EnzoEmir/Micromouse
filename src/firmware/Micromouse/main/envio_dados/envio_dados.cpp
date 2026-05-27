#include "envio_dados.hpp"

#include <cstring>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char* TAG = "ENVIO_DADOS";

const char* tipo_envio_para_string(TipoEnvio tipo) {
    switch (tipo) {
        case TipoEnvio::AvancoTile:       return "avanco_tile";
        case TipoEnvio::Heartbeat:        return "heartbeat";
        case TipoEnvio::InicioMapeamento: return "inicio_mapeamento";
        case TipoEnvio::FinalMapeamento:  return "final_mapeamento";
        case TipoEnvio::InicioCorrida:    return "inicio_corrida";
        case TipoEnvio::FinalCorrida:     return "final_corrida";
    }
    return "desconhecido";
}

// Serializa a grade do labirinto como um array 2D (linha y x coluna x) com a
// bitmask de paredes de cada célula. Retorna nullptr em caso de falha de alocação.
static cJSON* serializar_matriz(const Labirinto& labirinto) {
    const uint8_t n = labirinto.tamanho();

    cJSON* matriz = cJSON_CreateArray();
    if (matriz == nullptr) {
        return nullptr;
    }

    for (uint8_t y = 0; y < n; ++y) {
        cJSON* linha = cJSON_CreateArray();
        if (linha == nullptr) {
            cJSON_Delete(matriz);
            return nullptr;
        }
        for (uint8_t x = 0; x < n; ++x) {
            const Labirinto::Celula& c = labirinto.celula(x, y);
            cJSON_AddItemToArray(linha, cJSON_CreateNumber(c.walls));
        }
        cJSON_AddItemToArray(matriz, linha);
    }

    return matriz;
}

esp_err_t enviar_dados_sensores(const char* url,
                                const DadosEnvio& dados,
                                const Labirinto& labirinto) {
    if (url == nullptr) {
        ESP_LOGE(TAG, "URL nula");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON* raiz = cJSON_CreateObject();
    if (raiz == nullptr) {
        ESP_LOGE(TAG, "Falha ao criar objeto JSON raiz");
        return ESP_ERR_NO_MEM;
    }

    // Campos escalares (formato plano, todos no nível raiz)
    cJSON_AddStringToObject(raiz, "tipo", tipo_envio_para_string(dados.tipo));
    cJSON_AddNumberToObject(raiz, "velocidade_media_cms", dados.velocidade_media_cms);
    cJSON_AddStringToObject(raiz, "direcao", dados.direcao != nullptr ? dados.direcao : "N");
    cJSON_AddNumberToObject(raiz, "temperatura", dados.temperatura);
    cJSON_AddNumberToObject(raiz, "soc", dados.soc);
    cJSON_AddNumberToObject(raiz, "timestamp_ms", static_cast<double>(dados.timestamp_ms));

    // Matriz do labirinto + dimensão (facilita o parse no backend)
    cJSON* matriz = serializar_matriz(labirinto);
    if (matriz == nullptr) {
        ESP_LOGE(TAG, "Falha ao serializar matriz do labirinto");
        cJSON_Delete(raiz);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(raiz, "labirinto", matriz);
    cJSON_AddNumberToObject(raiz, "labirinto_tamanho", labirinto.tamanho());

    char* json_string = cJSON_PrintUnformatted(raiz);
    cJSON_Delete(raiz);
    if (json_string == nullptr) {
        ESP_LOGE(TAG, "Falha ao serializar JSON");
        return ESP_FAIL;
    }

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Falha ao inicializar o cliente HTTP");
        cJSON_free(json_string);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_string, strlen(json_string));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        const int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "POST enviado. Status HTTP: %d", status);
    } else {
        ESP_LOGE(TAG, "Falha no POST: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_free(json_string);
    return err;
}
