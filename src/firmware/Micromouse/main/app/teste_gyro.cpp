/* Teste isolado: GIRA NO LUGAR 90 graus em MALHA FECHADA pelo GIROSCOPIO.
 *
 * Sequencia:
 *   1) Espera 3 s (tirar a mao) e calibra o bias do gyro (robo PARADO).
 *   2) Gira ate o giroscopio acusar ~90 graus para a DIREITA e para.
 *   3) Espera alguns segundos.
 *   4) Gira ate ~90 graus para a ESQUERDA (volta a origem) e para.
 *   5) Fica parado.
 *
 * Uso:
 *   1) No main/CMakeLists.txt, deixe ativo apenas "app/teste_gyro.cpp" na
 *      lista de SRCS (comente as demais, p.ex. combined_test.cpp).
 *   2) idf.py build flash monitor
 *
 * COMO FUNCIONA:
 *   Lemos o MPU9250 DIRETO por I2C (igual ao combined_test.cpp), nao pela lib
 *   imu/imu.h -- a leitura direta e confiavel e ja vem com sinal correto. O
 *   gyro esta em +-250 dps => 131 LSB por grau/s. Integramos a velocidade
 *   angular para obter o angulo e PARAMOS quando ele atinge ~90 graus. Apos
 *   parar, seguimos medindo por um instante para registrar o angulo FINAL
 *   (inclui a inercia), que e impresso no log para conferencia.
 *
 *   Ha um TIMEOUT de seguranca: o robo nunca gira para sempre.
 *
 * CALIBRACAO:
 *   - PWM_GIRO controla a velocidade (pedido: 25%).
 *   - Se o angulo final passar de 90, aumente MARGEM_PARADA_GRAUS (para antes).
 *   - Se em PWM 25% o robo nao vencer o atrito e nao girar, aumente PWM_GIRO.
 */

#include <cstdio>
#include <cmath>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pins.hpp"
#include "i2c_manager.hpp"

static const char *TAG = "TESTE_GIRO";

namespace {

// --- Enderecos / registradores do MPU9250 ---
constexpr uint8_t MPU9250_ADDR        = 0x68;
constexpr uint8_t MPU_REG_PWR_MGMT_1  = 0x6B;
constexpr uint8_t MPU_REG_SMPLRT_DIV  = 0x19;
constexpr uint8_t MPU_REG_CONFIG      = 0x1A;
constexpr uint8_t MPU_REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t MPU_REG_ACCEL_CONFIG  = 0x1C;
constexpr uint8_t MPU_REG_ACCEL_CONFIG2 = 0x1D;
constexpr uint8_t MPU_REG_ACCEL_XOUT_H  = 0x3B;
constexpr float   GYRO_LSB_POR_DPS    = 131.0f; // +-250 dps

// --- Parametros de motor / giro ---
constexpr int     PWM_MAX        = 1023;                   // resolucao 10 bits
constexpr int     PWM_GIRO       = (int)(PWM_MAX * 0.25f); // 25% (pedido)
constexpr int     ESPERA_MS      = 3000;     // tempo para tirar a mao
constexpr int     PAUSA_ENTRE_MS = 2000;     // pausa entre os dois giros
constexpr float   ALVO_GRAUS     = 90.0f;    // angulo alvo de cada giro
constexpr float   MARGEM_PARADA_GRAUS = 5.0f;// para antes p/ compensar inercia (CALIBRAR)
constexpr int     SETTLE_MS      = 400;      // mede o angulo final apos parar
constexpr int64_t TIMEOUT_US     = 8000000;  // 8 s: trava de seguranca por giro

i2c_master_dev_handle_t g_mpu = nullptr;
bool  g_mpu_ok  = false;
float g_bias_z  = 0.0f;   // bias do gyro_z (em LSB)

// --- Auxiliares I2C ---
esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value) {
    if (!dev) return ESP_FAIL;
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(dev, data, sizeof(data), pdMS_TO_TICKS(100));
}
esp_err_t read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len) {
    if (!dev) return ESP_FAIL;
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, pdMS_TO_TICKS(100));
}
int16_t be_i16(const uint8_t *d) { return (int16_t)((d[0] << 8) | d[1]); } // MPU = big-endian

// --- Controle raw dos motores ---
void pwm_left(uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
void pwm_right(uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

// Gira no LUGAR para a DIREITA: esquerda p/ frente, direita p/ tras.
void dir_girar_direita() {
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 0);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 1);
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 1);
}
// Gira no LUGAR para a ESQUERDA: esquerda p/ tras, direita p/ frente.
void dir_girar_esquerda() {
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 1);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 1);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 0);
}
void motores_para() {
    pwm_left(0);
    pwm_right(0);
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 0);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 0);
}

void initMotores() {
    gpio_config_t cfg = {};
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pin_bit_mask = (1ULL << MOTOR_STBY_PIN) |
                       (1ULL << MOTOR_RIGHT_IN1_PIN) | (1ULL << MOTOR_RIGHT_IN2_PIN) |
                       (1ULL << MOTOR_LEFT_IN1_PIN)  | (1ULL << MOTOR_LEFT_IN2_PIN);
    gpio_config(&cfg);
    gpio_set_level(MOTOR_STBY_PIN, 0);

    ledc_timer_config_t t = {};
    t.speed_mode      = LEDC_LOW_SPEED_MODE;
    t.timer_num       = LEDC_TIMER_0;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.freq_hz         = 5000;
    t.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&t);

    ledc_channel_config_t ch_l = {};
    ch_l.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_l.channel    = LEDC_CHANNEL_0;
    ch_l.timer_sel  = LEDC_TIMER_0;
    ch_l.gpio_num   = MOTOR_LEFT_PWM_PIN;
    ch_l.duty       = 0;
    ledc_channel_config(&ch_l);

    ledc_channel_config_t ch_r = {};
    ch_r.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_r.channel    = LEDC_CHANNEL_1;
    ch_r.timer_sel  = LEDC_TIMER_0;
    ch_r.gpio_num   = MOTOR_RIGHT_PWM_PIN;
    ch_r.duty       = 0;
    ledc_channel_config(&ch_r);

    gpio_set_level(MOTOR_STBY_PIN, 1); // acorda a ponte H
}

// --- Giroscopio (MPU9250) via I2C direto ---
bool init_mpu() {
    if (!i2c_manager_register_device(MPU9250_ADDR, I2C_MANAGER_DEFAULT_SPEED_HZ, &g_mpu)) {
        ESP_LOGE(TAG, "Falha ao registrar MPU9250 no i2c_manager.");
        return false;
    }
    write_reg(g_mpu, MPU_REG_PWR_MGMT_1, 0x80);     // reset
    vTaskDelay(pdMS_TO_TICKS(100));
    write_reg(g_mpu, MPU_REG_PWR_MGMT_1, 0x01);     // acorda, clock interno
    vTaskDelay(pdMS_TO_TICKS(10));
    write_reg(g_mpu, MPU_REG_CONFIG,        0x03);  // DLPF
    write_reg(g_mpu, MPU_REG_SMPLRT_DIV,    0x04);  // ~200 Hz
    write_reg(g_mpu, MPU_REG_GYRO_CONFIG,   0x00);  // +-250 dps
    write_reg(g_mpu, MPU_REG_ACCEL_CONFIG,  0x00);  // +-2 g
    write_reg(g_mpu, MPU_REG_ACCEL_CONFIG2, 0x03);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "MPU9250 inicializado (gyro +-250 dps).");
    return true;
}

// Le o gyro_z cru (com sinal) em LSB. Retorna false em erro de I2C.
bool ler_gz_raw(int16_t *raw) {
    uint8_t data[14] = {0};
    if (read_regs(g_mpu, MPU_REG_ACCEL_XOUT_H, data, sizeof(data)) != ESP_OK) return false;
    *raw = be_i16(&data[12]); // gyro Z fica nos bytes 12-13
    return true;
}

// Le o gyro_z em graus/s (descontando o bias).
bool ler_gz_dps(float *dps) {
    int16_t raw;
    if (!ler_gz_raw(&raw)) return false;
    *dps = (raw - g_bias_z) / GYRO_LSB_POR_DPS;
    return true;
}

// Calcula o bias do gyro_z com o robo PARADO.
void calibrar_bias_z() {
    constexpr int N = 400;
    double soma = 0.0;
    int    n    = 0;
    for (int i = 0; i < N; ++i) {
        int16_t raw;
        if (ler_gz_raw(&raw)) { soma += raw; ++n; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    g_bias_z = (n > 0) ? (float)(soma / n) : 0.0f;
    ESP_LOGI(TAG, "Bias gyro_z = %.2f LSB (%d amostras)", g_bias_z, n);
}

// Gira no lugar ate o gyro acusar ~ALVO_GRAUS no sentido pedido (ou timeout).
void girar(bool sentido_direita) {
    if (!g_mpu_ok) {
        ESP_LOGE(TAG, "Giroscopio indisponivel; abortando giro.");
        return;
    }
    const char *nome = sentido_direita ? "DIREITA" : "ESQUERDA";
    ESP_LOGI(TAG, ">> Girando para a %s ate %.0f graus (PWM %d)...", nome, ALVO_GRAUS, PWM_GIRO);

    if (sentido_direita) dir_girar_direita();
    else                 dir_girar_esquerda();
    pwm_left(PWM_GIRO);
    pwm_right(PWM_GIRO);

    float   angulo = 0.0f;                  // angulo acumulado (graus)
    int64_t t_prev = esp_timer_get_time();
    const int64_t t0 = t_prev;
    bool timeout = false;

    // Fase 1: gira ate atingir o alvo (com margem) ou estourar o timeout.
    while (fabsf(angulo) < ALVO_GRAUS - MARGEM_PARADA_GRAUS) {
        if (esp_timer_get_time() - t0 > TIMEOUT_US) { timeout = true; break; }

        const int64_t now = esp_timer_get_time();
        float dt = (now - t_prev) / 1e6f;
        if (dt <= 0.0f) dt = 1e-3f;
        t_prev = now;

        float gz_dps;
        if (ler_gz_dps(&gz_dps)) angulo += gz_dps * dt;

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    motores_para();

    // Fase 2: continua medindo durante a parada para registrar o angulo final.
    const int64_t t_settle = esp_timer_get_time();
    while (esp_timer_get_time() - t_settle < (int64_t)SETTLE_MS * 1000) {
        const int64_t now = esp_timer_get_time();
        float dt = (now - t_prev) / 1e6f;
        if (dt <= 0.0f) dt = 1e-3f;
        t_prev = now;

        float gz_dps;
        if (ler_gz_dps(&gz_dps)) angulo += gz_dps * dt;

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (timeout) {
        ESP_LOGW(TAG, "<< TIMEOUT no giro %s. Angulo medido = %.1f graus (gyro pode ter falhado).",
                 nome, fabsf(angulo));
    } else {
        ESP_LOGI(TAG, "<< Fim do giro %s. Angulo FINAL medido pelo gyro = %.1f graus.",
                 nome, fabsf(angulo));
    }
}

} // namespace

extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "=== Teste giro 90 graus por giroscopio (PWM 25%%) ===");

    initMotores();
    motores_para();

    if (!i2c_manager_init()) {
        ESP_LOGE(TAG, "Falha no barramento I2C.");
    } else {
        g_mpu_ok = init_mpu();
    }

    ESP_LOGI(TAG, "Largada em %d ms... tire a mao e mantenha o robo PARADO.", ESPERA_MS);
    vTaskDelay(pdMS_TO_TICKS(ESPERA_MS));

    if (g_mpu_ok) {
        ESP_LOGI(TAG, "Calibrando bias do gyro (mantenha PARADO)...");
        calibrar_bias_z();
    }

    // 1) Gira 90 graus para a DIREITA.
    girar(/*sentido_direita=*/true);

    // 2) Pausa.
    ESP_LOGI(TAG, "Pausa de %d ms...", PAUSA_ENTRE_MS);
    vTaskDelay(pdMS_TO_TICKS(PAUSA_ENTRE_MS));

    // 3) Gira 90 graus para a ESQUERDA (volta para a origem).
    girar(/*sentido_direita=*/false);

    ESP_LOGI(TAG, "=== Fim do teste. Robo parado. ===");
    while (true) vTaskDelay(portMAX_DELAY);
}
