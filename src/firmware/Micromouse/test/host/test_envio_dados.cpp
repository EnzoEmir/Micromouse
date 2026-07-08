// Tests for envio_dados: verify each packet builder serializes the correct JSON
// and forwards the right result. HTTP is mocked and captures the POST body,
// which we parse back with cJSON to assert on structure.
#include "framework.hpp"

#include "envio_dados.hpp"
#include "esp_http_client.h"

extern "C" {
#include "cJSON.h"
}

static const char* URL = "http://example.test/api";

// Parses the last captured body and returns the cJSON tree (caller frees).
static cJSON* lastJson() { return cJSON_Parse(mock_http_last_body()); }

static double numField(cJSON* o, const char* k) {
    cJSON* it = cJSON_GetObjectItem(o, k);
    return (it && cJSON_IsNumber(it)) ? it->valuedouble : -99999.0;
}

static std::string strField(cJSON* o, const char* k) {
    cJSON* it = cJSON_GetObjectItem(o, k);
    return (it && cJSON_IsString(it)) ? std::string(it->valuestring) : std::string("<none>");
}

TEST_CASE(configuracao_inicial_packet) {
    mock_http_reset();
    esp_err_t r = enviar_configuracao_inicial(URL, 0, 16, "direita", 87);
    CHECK_EQ(r, ESP_OK);
    CHECK_EQ(mock_http_post_count(), 1);
    CHECK_EQ(std::string(mock_http_last_url()), std::string(URL));

    cJSON* j = lastJson();
    REQUIRE(j != nullptr);
    CHECK_EQ((int)numField(j, "tipo"), 0);
    CHECK_EQ((int)numField(j, "timestamp_ms"), 0);
    CHECK_EQ((int)numField(j, "dimensao"), 16);
    CHECK_EQ(strField(j, "lado_largada"), std::string("direita"));
    CHECK_EQ((int)numField(j, "bateria"), 87);
    cJSON_Delete(j);
}

TEST_CASE(movimentacao_packet) {
    mock_http_reset();
    esp_err_t r = enviar_movimentacao(URL, 1234, 3, 5, 0x0D);
    CHECK_EQ(r, ESP_OK);

    cJSON* j = lastJson();
    REQUIRE(j != nullptr);
    CHECK_EQ((int)numField(j, "tipo"), 1);
    CHECK_EQ((int)numField(j, "timestamp_ms"), 1234);
    CHECK_EQ((int)numField(j, "x"), 3);
    CHECK_EQ((int)numField(j, "y"), 5);
    CHECK_EQ((int)numField(j, "w"), 0x0D);
    cJSON_Delete(j);
}

TEST_CASE(rota_otimizada_packet) {
    mock_http_reset();
    Labirinto::Coordenada rota[3] = {{0, 0}, {1, 0}, {1, 1}};
    esp_err_t r = enviar_rota_otimizada(URL, 50, rota, 3);
    CHECK_EQ(r, ESP_OK);

    cJSON* j = lastJson();
    REQUIRE(j != nullptr);
    CHECK_EQ((int)numField(j, "tipo"), 2);
    cJSON* arr = cJSON_GetObjectItem(j, "rota");
    REQUIRE(arr != nullptr);
    CHECK(cJSON_IsArray(arr));
    CHECK_EQ(cJSON_GetArraySize(arr), 3);
    // Each entry is a [x,y] pair.
    cJSON* p1 = cJSON_GetArrayItem(arr, 1);
    REQUIRE(p1 != nullptr);
    CHECK_EQ(cJSON_GetArraySize(p1), 2);
    CHECK_EQ((int)cJSON_GetArrayItem(p1, 0)->valuedouble, 1);
    CHECK_EQ((int)cJSON_GetArrayItem(p1, 1)->valuedouble, 0);
    cJSON_Delete(j);
}

TEST_CASE(rota_otimizada_empty) {
    mock_http_reset();
    esp_err_t r = enviar_rota_otimizada(URL, 0, nullptr, 0);
    CHECK_EQ(r, ESP_OK);
    cJSON* j = lastJson();
    REQUIRE(j != nullptr);
    cJSON* arr = cJSON_GetObjectItem(j, "rota");
    REQUIRE(arr != nullptr);
    CHECK(cJSON_IsArray(arr));
    CHECK_EQ(cJSON_GetArraySize(arr), 0);
    cJSON_Delete(j);
}

TEST_CASE(fim_corrida_packet) {
    mock_http_reset();
    esp_err_t r = enviar_fim_corrida(URL, 9999, true, 1.25f, 40);
    CHECK_EQ(r, ESP_OK);

    cJSON* j = lastJson();
    REQUIRE(j != nullptr);
    CHECK_EQ((int)numField(j, "tipo"), 3);
    cJSON* suc = cJSON_GetObjectItem(j, "sucesso");
    REQUIRE(suc != nullptr);
    CHECK(cJSON_IsBool(suc));
    CHECK(cJSON_IsTrue(suc));
    CHECK_FLOAT_EQ(numField(j, "v_med"), 1.25, 1e-4);
    CHECK_EQ((int)numField(j, "bateria"), 40);
    cJSON_Delete(j);
}

TEST_CASE(heartbeat_packet) {
    mock_http_reset();
    esp_err_t r = enviar_heartbeat(URL, 200, 55);
    CHECK_EQ(r, ESP_OK);
    cJSON* j = lastJson();
    REQUIRE(j != nullptr);
    CHECK_EQ((int)numField(j, "tipo"), 4);
    CHECK_EQ((int)numField(j, "bateria"), 55);
    cJSON_Delete(j);
}

TEST_CASE(alerta_temperatura_packet) {
    mock_http_reset();
    esp_err_t r = enviar_alerta_temperatura(URL, 300, 71.5f);
    CHECK_EQ(r, ESP_OK);
    cJSON* j = lastJson();
    REQUIRE(j != nullptr);
    CHECK_EQ((int)numField(j, "tipo"), 5);
    CHECK_FLOAT_EQ(numField(j, "temp_c"), 71.5, 1e-3);
    cJSON_Delete(j);
}

TEST_CASE(null_url_is_rejected_without_post) {
    mock_http_reset();
    esp_err_t r = enviar_heartbeat(nullptr, 0, 50);
    CHECK_EQ(r, ESP_ERR_INVALID_ARG);
    CHECK_EQ(mock_http_post_count(), 0);  // nothing sent
}

TEST_CASE(perform_failure_is_propagated) {
    mock_http_reset();
    mock_http_set_perform_result(ESP_FAIL);
    esp_err_t r = enviar_heartbeat(URL, 0, 50);
    CHECK_EQ(r, ESP_FAIL);
    CHECK_EQ(mock_http_post_count(), 1);  // it tried
}

TEST_MAIN()
