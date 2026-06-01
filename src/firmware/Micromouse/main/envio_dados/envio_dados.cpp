#include "envio_dados.hpp"

#include <cstring>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char* TAG = "ENVIO_DADOS";

// Serializa `raiz` e faz o POST para `url`. Sempre consome (deleta) `raiz`,
// inclusive em caso de erro, para não vazar a árvore JSON.
static esp_err_t enviar_json(const char* url, cJSON* raiz) {
    if (raiz == nullptr) {
        ESP_LOGE(TAG, "Falha ao montar o objeto JSON");
        return ESP_ERR_NO_MEM;
    }
    if (url == nullptr) {
        ESP_LOGE(TAG, "URL nula");
        cJSON_Delete(raiz);
        return ESP_ERR_INVALID_ARG;
    }

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

// Cria o objeto base com os campos comuns "tipo" e "timestamp_ms", presentes
// em todos os pacotes. Retorna nullptr em caso de falha de alocação.
static cJSON* novo_pacote(TipoPacote tipo, int64_t timestamp_ms) {
    cJSON* raiz = cJSON_CreateObject();
    if (raiz == nullptr) {
        return nullptr;
    }
    cJSON_AddNumberToObject(raiz, "tipo", static_cast<int>(tipo));
    cJSON_AddNumberToObject(raiz, "timestamp_ms", static_cast<double>(timestamp_ms));
    return raiz;
}

esp_err_t enviar_configuracao_inicial(const char* url, int64_t timestamp_ms,
                                      int dimensao, int bateria) {
    cJSON* raiz = novo_pacote(TipoPacote::ConfiguracaoInicial, timestamp_ms);
    if (raiz != nullptr) {
        cJSON_AddNumberToObject(raiz, "dimensao", dimensao);
        cJSON_AddNumberToObject(raiz, "bateria", bateria);
    }
    return enviar_json(url, raiz);
}

esp_err_t enviar_movimentacao(const char* url, int64_t timestamp_ms,
                              int x, int y, uint8_t w) {
    cJSON* raiz = novo_pacote(TipoPacote::Movimentacao, timestamp_ms);
    if (raiz != nullptr) {
        cJSON_AddNumberToObject(raiz, "x", x);
        cJSON_AddNumberToObject(raiz, "y", y);
        cJSON_AddNumberToObject(raiz, "w", w);
    }
    return enviar_json(url, raiz);
}

esp_err_t enviar_rota_otimizada(const char* url, int64_t timestamp_ms,
                                const Labirinto::Coordenada* rota, uint16_t n) {
    cJSON* raiz = novo_pacote(TipoPacote::RotaOtimizada, timestamp_ms);
    if (raiz == nullptr) {
        return enviar_json(url, raiz);  // loga e retorna ESP_ERR_NO_MEM
    }

    cJSON* arr = cJSON_CreateArray();
    if (arr == nullptr) {
        ESP_LOGE(TAG, "Falha ao criar o array da rota");
        cJSON_Delete(raiz);
        return ESP_ERR_NO_MEM;
    }
    if (rota != nullptr) {
        for (uint16_t i = 0; i < n; ++i) {
            cJSON* par = cJSON_CreateArray();
            if (par == nullptr) {
                ESP_LOGE(TAG, "Falha ao criar par [x,y] da rota");
                cJSON_Delete(arr);
                cJSON_Delete(raiz);
                return ESP_ERR_NO_MEM;
            }
            cJSON_AddItemToArray(par, cJSON_CreateNumber(rota[i].x));
            cJSON_AddItemToArray(par, cJSON_CreateNumber(rota[i].y));
            cJSON_AddItemToArray(arr, par);
        }
    }
    cJSON_AddItemToObject(raiz, "rota", arr);
    return enviar_json(url, raiz);
}

esp_err_t enviar_fim_corrida(const char* url, int64_t timestamp_ms,
                             bool sucesso, float v_med, int bateria) {
    cJSON* raiz = novo_pacote(TipoPacote::FimDeCorrida, timestamp_ms);
    if (raiz != nullptr) {
        cJSON_AddBoolToObject(raiz, "sucesso", sucesso);
        cJSON_AddNumberToObject(raiz, "v_med", v_med);
        cJSON_AddNumberToObject(raiz, "bateria", bateria);
    }
    return enviar_json(url, raiz);
}

esp_err_t enviar_heartbeat(const char* url, int64_t timestamp_ms, int bateria) {
    cJSON* raiz = novo_pacote(TipoPacote::Heartbeat, timestamp_ms);
    if (raiz != nullptr) {
        cJSON_AddNumberToObject(raiz, "bateria", bateria);
    }
    return enviar_json(url, raiz);
}

esp_err_t enviar_alerta_temperatura(const char* url, int64_t timestamp_ms, float temp_c) {
    cJSON* raiz = novo_pacote(TipoPacote::AlertaTemperatura, timestamp_ms);
    if (raiz != nullptr) {
        cJSON_AddNumberToObject(raiz, "temp_c", temp_c);
    }
    return enviar_json(url, raiz);
}
