// Tests for the Telemetria orchestration layer: Wi-Fi gating, relative
// timestamps, and the inactivity heartbeat. envio_dados and wifi are mocked, and
// the clock is virtual, so the decision logic is tested in isolation.
#include "framework.hpp"

#include "telemetria.hpp"
#include "maze/maze.hpp"
#include "wifi.hpp"
#include "esp_timer.h"
#include "mock_envio_dados.h"

static const char* URL = "http://example.test/api";

static void resetAll() {
    mock_envio_reset();
    mock_wifi_reset();
    mock_timer_reset();
}

static Labirinto make16() {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k16x16);
    return lab;
}

TEST_CASE(inicializar_connects_and_sends_config) {
    resetAll();
    Labirinto lab = make16();
    Telemetria t(URL);
    t.inicializar("ssid", "pass", lab, 90);

    CHECK_EQ(mock_wifi_init_count(), 1);
    const EnvioLog* log = mock_envio_get_log();
    CHECK_EQ(log->count_by_tipo[0], 1);   // ConfiguracaoInicial
    CHECK_EQ(log->last_dimensao, 16);
    // Labirinto padrao comeca em {0,0} (canto oeste) -> largada pela esquerda.
    CHECK_EQ(std::string(log->last_lado_largada), std::string("esquerda"));
    CHECK_EQ(log->last_bateria, 90);
    CHECK_EQ((int)log->last_timestamp_ms, 0);  // clock zeroed at start
}

TEST_CASE(timestamp_is_relative_to_start) {
    resetAll();
    Labirinto lab = make16();
    // Start the run at t = 10 s.
    mock_timer_set_us(10 * 1000000LL);
    Telemetria t(URL);
    t.inicializar("s", "p", lab, 100);

    // Advance to t = 12.5 s and move.
    mock_timer_set_us(12500 * 1000LL);
    esp_err_t r = t.movimento(3, 4, 0x05);
    CHECK_EQ(r, ESP_OK);
    const EnvioLog* log = mock_envio_get_log();
    CHECK_EQ(log->count_by_tipo[1], 1);
    CHECK_EQ((int)log->last_timestamp_ms, 2500);  // 12.5s - 10s
    CHECK_EQ(log->last_x, 3);
    CHECK_EQ(log->last_y, 4);
    CHECK_EQ((int)log->last_w, 0x05);
}

TEST_CASE(movimento_blocked_when_disconnected) {
    resetAll();
    Labirinto lab = make16();
    Telemetria t(URL);
    t.inicializar("s", "p", lab, 100);
    int before = mock_envio_get_log()->total_calls;

    mock_wifi_set_connected(false);
    esp_err_t r = t.movimento(1, 1, 0);
    CHECK_EQ(r, ESP_FAIL);
    CHECK_EQ(mock_envio_get_log()->total_calls, before);  // nothing sent
}

TEST_CASE(fim_corrida_and_alerta_packets) {
    resetAll();
    Labirinto lab = make16();
    Telemetria t(URL);
    t.inicializar("s", "p", lab, 100);

    esp_err_t r1 = t.fim_corrida(true, 1.5f, 30);
    CHECK_EQ(r1, ESP_OK);
    const EnvioLog* log = mock_envio_get_log();
    CHECK_EQ(log->count_by_tipo[3], 1);
    CHECK(log->last_sucesso);
    CHECK_FLOAT_EQ(log->last_v_med, 1.5f, 1e-4);
    CHECK_EQ(log->last_bateria, 30);

    esp_err_t r2 = t.alerta_temperatura(72.0f);
    CHECK_EQ(r2, ESP_OK);
    CHECK_EQ(log->count_by_tipo[5], 1);
    CHECK_FLOAT_EQ(log->last_temp_c, 72.0f, 1e-3);
}

TEST_CASE(heartbeat_fires_only_after_timeout) {
    resetAll();
    Labirinto lab = make16();
    Telemetria t(URL, /*heartbeat_ms=*/1500);
    t.inicializar("s", "p", lab, 100);  // last send registered at t=0
    int hb0 = mock_envio_get_log()->count_by_tipo[4];

    // Just under the timeout: no heartbeat.
    mock_timer_set_us(1499 * 1000LL);
    t.verificar_heartbeat(80);
    CHECK_EQ(mock_envio_get_log()->count_by_tipo[4], hb0);

    // At the timeout: heartbeat sent.
    mock_timer_set_us(1500 * 1000LL);
    t.verificar_heartbeat(80);
    CHECK_EQ(mock_envio_get_log()->count_by_tipo[4], hb0 + 1);
    CHECK_EQ(mock_envio_get_log()->last_bateria, 80);
}

TEST_CASE(heartbeat_suppressed_when_disconnected) {
    resetAll();
    Labirinto lab = make16();
    Telemetria t(URL, 1000);
    t.inicializar("s", "p", lab, 100);
    int hb0 = mock_envio_get_log()->count_by_tipo[4];

    mock_wifi_set_connected(false);
    mock_timer_set_us(5000 * 1000LL);  // well past the timeout
    t.verificar_heartbeat(80);
    CHECK_EQ(mock_envio_get_log()->count_by_tipo[4], hb0);  // still nothing
}

TEST_CASE(activity_resets_heartbeat_timer) {
    resetAll();
    Labirinto lab = make16();
    Telemetria t(URL, 1500);
    t.inicializar("s", "p", lab, 100);
    int hb0 = mock_envio_get_log()->count_by_tipo[4];

    // A movement at t=1000ms refreshes the "last send" timestamp.
    mock_timer_set_us(1000 * 1000LL);
    t.movimento(0, 1, 0);

    // At t=2000ms only 1000ms have elapsed since that send (< 1500) -> no HB.
    mock_timer_set_us(2000 * 1000LL);
    t.verificar_heartbeat(50);
    CHECK_EQ(mock_envio_get_log()->count_by_tipo[4], hb0);

    // At t=2600ms, 1600ms have elapsed -> heartbeat.
    mock_timer_set_us(2600 * 1000LL);
    t.verificar_heartbeat(50);
    CHECK_EQ(mock_envio_get_log()->count_by_tipo[4], hb0 + 1);
}

TEST_MAIN()
