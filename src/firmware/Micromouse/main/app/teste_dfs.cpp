/* Navegacao por TILE (regra da mao esquerda) + PARADA no 2x2 aberto.
 *
 * Base: navegador reativo (junta teste_gyro + teste_para + teste_tof +
 * teste_tof_lateral) que anda 1 tile por vez de forma confiavel. Por cima,
 * mantemos um MAPA 4x4 (posicao/rumo + paredes) para DETECTAR e PARAR quando
 * for descoberto um bloco 2x2 de celulas SEM nenhuma parede interna.
 *
 * Ciclo por CELULA (tile de 18 cm), repete ate achar o 2x2:
 *   1) OLHA as paredes (ToFs esq/frente/dir), REGISTRA no mapa e checa o 2x2.
 *   2) DECIDE pela regra da mao esquerda (esq livre -> esq; senao frente ->
 *      reto; senao dir -> dir; senao 180). Vira com o GIROSCOPIO.
 *   3) ANDA exatamente 1 TILE usando o ToF FRONTAL como "regua", centralizado
 *      pelos laterais. Atualiza a posicao no grid.
 *
 * Uso:
 *   1) No main/CMakeLists.txt, deixe ativo apenas "app/teste_dfs.cpp".
 *   2) idf.py build flash monitor
 *
 * Mapa: celula (x,y), x=coluna 0..3 (Leste+), y=linha 0..3 (Norte+). Largada
 * em (0,0) apontando para o NORTE. Direcoes: N=0, E=1, S=2, W=3.
 *
 * Sensores (mapeamento fisico real, descoberto na bancada):
 *   FRONTAL  = FRONT_LEFT  (0x2B) | ESQUERDA = FRONT (0x2A) | DIREITA = FRONT_RIGHT (0x2C)
 *   GIRO     = MPU9250 (0x68) lido DIRETO por I2C (gyro +-250 dps, 131 LSB/dps)
 */

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pins.hpp"
#include "i2c_manager.hpp"
#include "battery/battery.hpp"
#include "vl53l0x/IV_Vl53l0x.hpp"

static const char *TAG = "TESTE_NAV";

namespace {

// =====================  PARAMETROS  =====================

constexpr int   PWM_MAX     = 1023;                  // resolucao 10 bits
constexpr int   GRID        = 4;                     // labirinto 4x4

// --- Avanco ---
constexpr int   PWM_DRIVE   = (int)(PWM_MAX * 0.25f);// velocidade de avanco (25%)
constexpr float TRIM_R      = 1.05f;                 // feed-forward do motor direito
constexpr int   PWM_DRIVE_R = (int)(PWM_DRIVE * TRIM_R) > PWM_MAX
                                  ? PWM_MAX : (int)(PWM_DRIVE * TRIM_R);
constexpr int   ESPERA_MS   = 3000;                  // tempo para tirar a mao

// --- ToF frontal ---
constexpr float BIAS_FRENTE   = 69.0f;   // lida - real (caracterizado)
constexpr float FRONT_STOP_MM = 50.0f;   // para se a parede estiver a <= 5 cm
constexpr int   CONFIRMACOES  = 2;        // leituras seguidas p/ confirmar parede

// --- Recentralizacao frontal (pulsos) ---
constexpr float TARGET_MM   = 30.0f;     // distancia frontal final desejada
constexpr float TOL_MM      = 5.0f;      // faixa aceitavel
constexpr int   PWM_AJUSTE  = PWM_MAX / 3;
constexpr int   PWM_AJUSTE_R = (int)(PWM_AJUSTE * TRIM_R) > PWM_MAX
                                  ? PWM_MAX : (int)(PWM_AJUSTE * TRIM_R);
constexpr int   PULSO_MS    = 70;
constexpr int   ASSENTA_MS  = 250;
constexpr int   MAX_AJUSTES = 25;

// --- ToFs laterais ---
constexpr float BIAS_ESQ = 55.0f;        // lida - real (esquerda)
constexpr float BIAS_DIR = 52.0f;        // lida - real (direita)
constexpr int   N_AMOSTRAS = 7;
constexpr float WALL_THRESHOLD_MM = 100.0f; // < isto = ha parede do lado
constexpr float PAREDE_ALVO_MM    = 50.0f;  // distancia desejada de cada parede

// --- Movimento por TILE (celula de 18 cm), medido pelo ToF FRONTAL ---
constexpr float   TILE_MM         = 180.0f;   // 18 cm por celula (CALIBRAR)
constexpr float   TILE_TOL_MM     = 12.0f;    // tolerancia de chegada no tile
constexpr float   FRENTE_LIVRE_MM = 120.0f;   // df acima disso = frente ABERTA
constexpr int64_t TILE_TIMEOUT_US = 5000000;  // trava de seguranca por tile

// --- Centralizacao durante o avanco (proporcional, bem suave) ---
constexpr float KP_CENTRO    = 0.5f;     // PWM por mm de erro lateral (CALIBRAR)
constexpr int   CORR_MAX     = 25;       // saturacao da correcao
constexpr float FILTRO_CORR  = 0.08f;    // passa-baixa da correcao
constexpr float ZONA_MORTA_MM = 8.0f;    // ignora erros pequenos

// --- Giroscopio / giro 90 graus ---
constexpr uint8_t MPU9250_ADDR        = 0x68;
constexpr uint8_t MPU_REG_PWR_MGMT_1  = 0x6B;
constexpr uint8_t MPU_REG_SMPLRT_DIV  = 0x19;
constexpr uint8_t MPU_REG_CONFIG      = 0x1A;
constexpr uint8_t MPU_REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t MPU_REG_ACCEL_CONFIG  = 0x1C;
constexpr uint8_t MPU_REG_ACCEL_CONFIG2 = 0x1D;
constexpr uint8_t MPU_REG_ACCEL_XOUT_H  = 0x3B;
constexpr float   GYRO_LSB_POR_DPS    = 131.0f;
constexpr int     PWM_GIRO            = (int)(PWM_MAX * 0.30f); // 30%
constexpr float   ALVO_GRAUS          = 90.0f;
constexpr float   MARGEM_PARADA_GRAUS = 10.0f;  // para antes (inercia) - CALIBRAR
constexpr int     SETTLE_MS           = 400;
constexpr int64_t GIRO_TIMEOUT_US     = 8000000; // trava de seguranca por giro

// =====================  ESTADO GLOBAL  =====================

Battery g_battery;
i2c_master_dev_handle_t g_mpu = nullptr;
bool  g_mpu_ok = false;
float g_bias_z = 0.0f;

// Mapa / pose no grid (para detectar o 2x2).
int  g_x = 0, g_y = 0, g_h = 0;       // h: 0=N,1=E,2=S,3=W
bool g_stop = false;                  // achou o 2x2 -> encerra
uint8_t g_wall[GRID][GRID]    = {};   // bit por direcao: ha parede
uint8_t g_known[GRID][GRID]   = {};   // bit por direcao: lado ja conhecido
bool    g_visited[GRID][GRID] = {};
constexpr int DX[4] = {0, 1, 0, -1};  // N,E,S,W
constexpr int DY[4] = {1, 0, -1, 0};
const char *DIR_NOME[4] = {"N", "E", "S", "W"};

// FRONTAL = FRONT_LEFT(0x2B) | ESQUERDA = FRONT(0x2A) | DIREITA = FRONT_RIGHT(0x2C)
IV_Vl53l0x g_tof_front({
    .position = IV_Vl53l0x::Position::FRONT,
    .address = I2C_ADDR_VL53L0X_ALT_1, .xshut_pin = TOF_FRONT_LEFT_XSHUT_PIN,
    .i2c_speed_hz = I2C_MANAGER_DEFAULT_SPEED_HZ, .timing_budget_ms = 20,
    .log_level = espp::Logger::Verbosity::WARN,
});
IV_Vl53l0x g_tof_esq({
    .position = IV_Vl53l0x::Position::LEFT,
    .address = I2C_ADDR_VL53L0X_ALT_0, .xshut_pin = TOF_FRONT_XSHUT_PIN,
    .i2c_speed_hz = I2C_MANAGER_DEFAULT_SPEED_HZ, .timing_budget_ms = 20,
    .log_level = espp::Logger::Verbosity::WARN,
});
IV_Vl53l0x g_tof_dir({
    .position = IV_Vl53l0x::Position::RIGHT,
    .address = I2C_ADDR_VL53L0X_ALT_2, .xshut_pin = TOF_FRONT_RIGHT_XSHUT_PIN,
    .i2c_speed_hz = I2C_MANAGER_DEFAULT_SPEED_HZ, .timing_budget_ms = 20,
    .log_level = espp::Logger::Verbosity::WARN,
});
// Declarados so para poderem ser desligados antes do boot (evita colisao 0x29).
IV_Vl53l0x g_tof_alt3({
    .position = IV_Vl53l0x::Position::LEFT,
    .address = I2C_ADDR_VL53L0X_ALT_3, .xshut_pin = TOF_LEFT_XSHUT_PIN,
    .i2c_speed_hz = I2C_MANAGER_DEFAULT_SPEED_HZ, .timing_budget_ms = 20,
    .log_level = espp::Logger::Verbosity::WARN,
});
IV_Vl53l0x g_tof_alt4({
    .position = IV_Vl53l0x::Position::RIGHT,
    .address = I2C_ADDR_VL53L0X_ALT_4, .xshut_pin = TOF_RIGHT_XSHUT_PIN,
    .i2c_speed_hz = I2C_MANAGER_DEFAULT_SPEED_HZ, .timing_budget_ms = 20,
    .log_level = espp::Logger::Verbosity::WARN,
});

// =====================  I2C / MPU  =====================

esp_err_t mpu_write(uint8_t reg, uint8_t value) {
    if (!g_mpu) return ESP_FAIL;
    uint8_t d[] = {reg, value};
    return i2c_master_transmit(g_mpu, d, sizeof(d), pdMS_TO_TICKS(100));
}
esp_err_t mpu_read(uint8_t reg, uint8_t *data, size_t len) {
    if (!g_mpu) return ESP_FAIL;
    return i2c_master_transmit_receive(g_mpu, &reg, 1, data, len, pdMS_TO_TICKS(100));
}
int16_t be_i16(const uint8_t *d) { return (int16_t)((d[0] << 8) | d[1]); }

bool init_mpu() {
    if (!i2c_manager_register_device(MPU9250_ADDR, I2C_MANAGER_DEFAULT_SPEED_HZ, &g_mpu)) {
        ESP_LOGE(TAG, "Falha ao registrar MPU9250.");
        return false;
    }
    mpu_write(MPU_REG_PWR_MGMT_1, 0x80); vTaskDelay(pdMS_TO_TICKS(100));
    mpu_write(MPU_REG_PWR_MGMT_1, 0x01); vTaskDelay(pdMS_TO_TICKS(10));
    mpu_write(MPU_REG_CONFIG,        0x03);
    mpu_write(MPU_REG_SMPLRT_DIV,    0x04);
    mpu_write(MPU_REG_GYRO_CONFIG,   0x00); // +-250 dps
    mpu_write(MPU_REG_ACCEL_CONFIG,  0x00);
    mpu_write(MPU_REG_ACCEL_CONFIG2, 0x03);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "MPU9250 OK (gyro +-250 dps).");
    return true;
}

bool ler_gz_raw(int16_t *raw) {
    uint8_t d[14] = {0};
    if (mpu_read(MPU_REG_ACCEL_XOUT_H, d, sizeof(d)) != ESP_OK) return false;
    *raw = be_i16(&d[12]); // gyro Z nos bytes 12-13
    return true;
}
bool ler_gz_dps(float *dps) {
    int16_t raw;
    if (!ler_gz_raw(&raw)) return false;
    *dps = (raw - g_bias_z) / GYRO_LSB_POR_DPS;
    return true;
}
void calibrar_bias_z() {
    constexpr int N = 400;
    double soma = 0.0; int n = 0;
    for (int i = 0; i < N; ++i) {
        int16_t raw;
        if (ler_gz_raw(&raw)) { soma += raw; ++n; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    g_bias_z = (n > 0) ? (float)(soma / n) : 0.0f;
    ESP_LOGI(TAG, "Bias gyro_z = %.2f LSB (%d amostras)", g_bias_z, n);
}

// =====================  MOTORES  =====================

void pwm_left(uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
void pwm_right(uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}
void dir_frente() {
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 0);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 1);
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 1);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 0);
}
void dir_re() {
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 1);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 1);
}
void dir_girar_direita() { // esquerda p/ frente, direita p/ tras
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 0);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 1);
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 1);
}
void dir_girar_esquerda() { // esquerda p/ tras, direita p/ frente
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 1);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 1);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 0);
}
void motores_para() {
    pwm_left(0); pwm_right(0);
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
    t.speed_mode = LEDC_LOW_SPEED_MODE; t.timer_num = LEDC_TIMER_0;
    t.duty_resolution = LEDC_TIMER_10_BIT; t.freq_hz = 5000; t.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&t);

    ledc_channel_config_t ch_l = {};
    ch_l.speed_mode = LEDC_LOW_SPEED_MODE; ch_l.channel = LEDC_CHANNEL_0;
    ch_l.timer_sel = LEDC_TIMER_0; ch_l.gpio_num = MOTOR_LEFT_PWM_PIN; ch_l.duty = 0;
    ledc_channel_config(&ch_l);

    ledc_channel_config_t ch_r = {};
    ch_r.speed_mode = LEDC_LOW_SPEED_MODE; ch_r.channel = LEDC_CHANNEL_1;
    ch_r.timer_sel = LEDC_TIMER_0; ch_r.gpio_num = MOTOR_RIGHT_PWM_PIN; ch_r.duty = 0;
    ledc_channel_config(&ch_r);

    gpio_set_level(MOTOR_STBY_PIN, 1); // acorda a ponte H
}

// =====================  ToF (distancias REAIS)  =====================

float lerReal(IV_Vl53l0x &tof, float bias) {
    const float bruto = tof.readDistanceMm();
    if (bruto <= 0.0f || bruto >= 9000.0f) return -1.0f;
    float real = bruto - bias;
    if (real < 0.0f) real = 0.0f;
    return real;
}
float lerEstavel(IV_Vl53l0x &tof, float bias, int n) {
    float v[16];
    if (n > 16) n = 16;
    int k = 0;
    for (int i = 0; i < n; ++i) {
        const float d = lerReal(tof, bias);
        if (d >= 0.0f) v[k++] = d;
    }
    if (k == 0) return -1.0f;
    std::sort(v, v + k);
    return v[k / 2];
}

// =====================  GIRO 90 GRAUS (gyro)  =====================

void girar(bool sentido_direita) {
    if (!g_mpu_ok) { ESP_LOGE(TAG, "Gyro indisponivel; giro abortado."); return; }
    const char *nome = sentido_direita ? "DIREITA" : "ESQUERDA";
    ESP_LOGI(TAG, ">> Girando 90 para a %s...", nome);

    if (sentido_direita) dir_girar_direita(); else dir_girar_esquerda();
    pwm_left(PWM_GIRO); pwm_right(PWM_GIRO);

    float   angulo = 0.0f;
    int64_t t_prev = esp_timer_get_time();
    const int64_t t0 = t_prev;
    bool timeout = false;

    // Fase 1: gira ate o alvo (com margem) ou timeout.
    while (fabsf(angulo) < ALVO_GRAUS - MARGEM_PARADA_GRAUS) {
        if (esp_timer_get_time() - t0 > GIRO_TIMEOUT_US) { timeout = true; break; }
        const int64_t now = esp_timer_get_time();
        float dt = (now - t_prev) / 1e6f;
        if (dt <= 0.0f) dt = 1e-3f;
        t_prev = now;
        float gz;
        if (ler_gz_dps(&gz)) angulo += gz * dt;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    motores_para();

    // Fase 2: mede o angulo final durante a parada (inclui inercia).
    const int64_t ts = esp_timer_get_time();
    while (esp_timer_get_time() - ts < (int64_t)SETTLE_MS * 1000) {
        const int64_t now = esp_timer_get_time();
        float dt = (now - t_prev) / 1e6f;
        if (dt <= 0.0f) dt = 1e-3f;
        t_prev = now;
        float gz;
        if (ler_gz_dps(&gz)) angulo += gz * dt;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (timeout) ESP_LOGW(TAG, "<< TIMEOUT giro %s. Angulo=%.1f", nome, fabsf(angulo));
    else         ESP_LOGI(TAG, "<< Giro %s ok. Angulo final=%.1f graus", nome, fabsf(angulo));
}

// =====================  MAPA DE PAREDES + 2x2  =====================

bool dentro(int x, int y) { return x >= 0 && x < GRID && y >= 0 && y < GRID; }

// Marca a parede (ou ausencia) na direcao d da celula (x,y) e na vizinha.
void setWall(int x, int y, int d, bool present) {
    if (!dentro(x, y)) return;
    const uint8_t b = (uint8_t)(1 << d);
    if (present) g_wall[x][y] |= b; else g_wall[x][y] &= (uint8_t)~b;
    g_known[x][y] |= b;
    const int nx = x + DX[d], ny = y + DY[d];
    if (dentro(nx, ny)) {
        const uint8_t ob = (uint8_t)(1 << ((d + 2) % 4));
        if (present) g_wall[nx][ny] |= ob; else g_wall[nx][ny] &= (uint8_t)~ob;
        g_known[nx][ny] |= ob;
    }
}

// Registra as paredes da celula atual a partir das aberturas observadas.
void registrarParedes(bool esq_aberto, bool frente_aberta, bool dir_aberto) {
    if (!dentro(g_x, g_y)) return;
    setWall(g_x, g_y, g_h,           !frente_aberta);  // frente
    setWall(g_x, g_y, (g_h + 3) % 4, !esq_aberto);     // esquerda
    setWall(g_x, g_y, (g_h + 1) % 4, !dir_aberto);     // direita
    g_visited[g_x][g_y] = true;
}

// Existe um bloco 2x2 totalmente visitado e SEM paredes internas?
bool achou2x2() {
    for (int bx = 0; bx < GRID - 1; ++bx) {
        for (int by = 0; by < GRID - 1; ++by) {
            if (!g_visited[bx][by] || !g_visited[bx + 1][by] ||
                !g_visited[bx][by + 1] || !g_visited[bx + 1][by + 1]) continue;
            const bool e_baixo = g_wall[bx][by]     & (1 << 1); // Leste de (bx,by)
            const bool e_cima  = g_wall[bx][by + 1] & (1 << 1); // Leste de (bx,by+1)
            const bool n_esq   = g_wall[bx][by]     & (1 << 0); // Norte de (bx,by)
            const bool n_dir   = g_wall[bx + 1][by] & (1 << 0); // Norte de (bx+1,by)
            if (!e_baixo && !e_cima && !n_esq && !n_dir) {
                ESP_LOGI(TAG, "*** 2x2 ABERTO: bloco (%d,%d)-(%d,%d) ***",
                         bx, by, bx + 1, by + 1);
                return true;
            }
        }
    }
    return false;
}

// =====================  PASSOS DO CICLO  =====================

enum class Avanco { TILE_OK, PAREDE, SEM_REF };

// Anda EXATAMENTE 1 tile (18 cm) usando o ToF FRONTAL como "regua", mantendo-se
// centralizado pelos laterais.
Avanco andarUmTile() {
    const float d0 = lerEstavel(g_tof_front, BIAS_FRENTE, 5);
    const bool  ref_front = (d0 >= 0.0f);
    const float alvo = ref_front ? (d0 - TILE_MM) : -1.0f;
    ESP_LOGI(TAG, "Tile: d0=%.0f mm %s", d0,
             ref_front ? "(regua frontal)" : "(sem parede a frente)");

    const float de_ini = lerReal(g_tof_esq, BIAS_ESQ);
    const float dd_ini = lerReal(g_tof_dir, BIAS_DIR);
    const bool  we_ini = (de_ini >= 0.0f && de_ini < WALL_THRESHOLD_MM);
    const bool  wd_ini = (dd_ini >= 0.0f && dd_ini < WALL_THRESHOLD_MM);

    dir_frente();
    const int64_t t0 = esp_timer_get_time();
    int   confirmados = 0;
    float corr_filt   = 0.0f;

    while (true) {
        const float df = lerReal(g_tof_front, BIAS_FRENTE);
        const float de = lerReal(g_tof_esq,  BIAS_ESQ);
        const float dd = lerReal(g_tof_dir,  BIAS_DIR);
        const bool  we = (de >= 0.0f && de < WALL_THRESHOLD_MM);
        const bool  wd = (dd >= 0.0f && dd < WALL_THRESHOLD_MM);

        // (a) Parede imediata: encostou antes de completar a celula.
        if (df >= 0.0f && df <= FRONT_STOP_MM) {
            if (++confirmados >= CONFIRMACOES) { motores_para(); return Avanco::PAREDE; }
        } else {
            confirmados = 0;
        }
        // (b) Regua frontal: a distancia caiu TILE_MM -> andou 1 celula.
        if (ref_front && df >= 0.0f && df <= alvo + TILE_TOL_MM) {
            motores_para();
            return Avanco::TILE_OK;
        }
        // (c) Sem parede a frente: conta a celula pela borda lateral.
        if (!ref_front && (we != we_ini || wd != wd_ini)) {
            motores_para();
            return Avanco::TILE_OK;
        }

        // Centralizacao pelos laterais (proporcional suave, com passa-baixa).
        float err = 0.0f;
        if (we && wd)      err = dd - de;
        else if (we)       err = PAREDE_ALVO_MM - de;
        else if (wd)       err = dd - PAREDE_ALVO_MM;
        if (std::fabs(err) < ZONA_MORTA_MM) err = 0.0f;

        float corr = KP_CENTRO * err;
        if (corr >  CORR_MAX) corr =  CORR_MAX;
        if (corr < -CORR_MAX) corr = -CORR_MAX;
        corr_filt += FILTRO_CORR * (corr - corr_filt);

        int dl = PWM_DRIVE   + (int)corr_filt;
        int dr = PWM_DRIVE_R - (int)corr_filt;
        if (dl < 0) dl = 0;
        if (dl > PWM_MAX) dl = PWM_MAX;
        if (dr < 0) dr = 0;
        if (dr > PWM_MAX) dr = PWM_MAX;
        pwm_left(dl); pwm_right(dr);

        // (d) Trava de seguranca por celula.
        if (esp_timer_get_time() - t0 > TILE_TIMEOUT_US) {
            motores_para();
            if (ref_front) { ESP_LOGW(TAG, "Tile: timeout com regua frontal."); return Avanco::TILE_OK; }
            ESP_LOGW(TAG, "Tile: timeout sem referencia (corredor aberto).");
            return Avanco::SEM_REF;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// Ajuste fino da distancia frontal: pulsos de re/frente ate TARGET_MM +/- TOL_MM.
void recentralizarFrontal() {
    ESP_LOGI(TAG, "Recentralizando frontal para %.0f +/- %.0f mm...", TARGET_MM, TOL_MM);
    for (int i = 0; i < MAX_AJUSTES; ++i) {
        const float d = lerEstavel(g_tof_front, BIAS_FRENTE, 5);
        if (d < 0.0f) { ESP_LOGW(TAG, "ajuste: leitura invalida"); continue; }
        const float err = d - TARGET_MM;
        if (std::fabs(err) <= TOL_MM) { ESP_LOGI(TAG, "frontal ok: %.0f mm", d); return; }

        if (err < 0.0f) dir_re(); else dir_frente();
        pwm_left(PWM_AJUSTE); pwm_right(PWM_AJUSTE_R);
        vTaskDelay(pdMS_TO_TICKS(PULSO_MS));
        motores_para();
        vTaskDelay(pdMS_TO_TICKS(ASSENTA_MS));
        ESP_LOGI(TAG, "ajuste %d: d=%.0f err=%+.0f -> %s", i + 1, d, err,
                 err < 0.0f ? "RE" : "frente");
    }
    ESP_LOGW(TAG, "ajuste frontal: nao convergiu");
}

// Olha as 3 paredes (robo PARADO), REGISTRA no mapa, checa o 2x2 e ja executa a
// virada (regra da mao esquerda). Atualiza o rumo g_h conforme o giro.
void decidirVirada() {
    const float de = lerEstavel(g_tof_esq,   BIAS_ESQ,    N_AMOSTRAS);
    const float dd = lerEstavel(g_tof_dir,   BIAS_DIR,    N_AMOSTRAS);
    const float df = lerEstavel(g_tof_front, BIAS_FRENTE, 5);
    const bool esq_aberto    = (de < 0.0f) || (de > WALL_THRESHOLD_MM);
    const bool dir_aberto    = (dd < 0.0f) || (dd > WALL_THRESHOLD_MM);
    const bool frente_aberta = (df < 0.0f) || (df > FRENTE_LIVRE_MM);
    ESP_LOGI(TAG, "(%d,%d) rumo %s | ESQ %s (%.0f) | FRENTE %s (%.0f) | DIR %s (%.0f)",
             g_x, g_y, DIR_NOME[g_h],
             esq_aberto    ? "livre" : "PAREDE", de,
             frente_aberta ? "livre" : "PAREDE", df,
             dir_aberto    ? "livre" : "PAREDE", dd);

    // --- Mapeia e checa o objetivo ANTES de mover ---
    registrarParedes(esq_aberto, frente_aberta, dir_aberto);
    if (achou2x2()) { g_stop = true; return; }

    // --- Regra da mao esquerda (vira e atualiza o rumo) ---
    if (esq_aberto) {
        girar(/*direita=*/false);
        g_h = (g_h + 3) % 4;
    } else if (frente_aberta) {
        ESP_LOGI(TAG, "Segue reto.");
    } else if (dir_aberto) {
        girar(/*direita=*/true);
        g_h = (g_h + 1) % 4;
    } else {
        ESP_LOGI(TAG, "Sem saida: girando 180.");
        girar(true); girar(true);
        g_h = (g_h + 2) % 4;
    }
}

} // namespace

extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "=== Navegacao por tile + parada no 2x2 aberto ===");

    // 1) Bateria (sobe o barramento I2C compartilhado).
    if (!g_battery.init()) {
        ESP_LOGE(TAG, "Falha ao inicializar bateria/I2C.");
        return;
    }

    // 2) Desliga os 5 ToFs e inicializa os 3 usados (frontal, esq, dir).
    g_tof_front.disable(); g_tof_esq.disable(); g_tof_dir.disable();
    g_tof_alt3.disable();  g_tof_alt4.disable();
    vTaskDelay(pdMS_TO_TICKS(20));
    bool ok_f = g_tof_front.init();
    bool ok_e = g_tof_esq.init();
    bool ok_d = g_tof_dir.init();
    if (!ok_f) ESP_LOGE(TAG, "Falha ToF FRONTAL");
    if (!ok_e) ESP_LOGE(TAG, "Falha ToF ESQUERDA");
    if (!ok_d) ESP_LOGE(TAG, "Falha ToF DIREITA");
    if (!ok_f || !ok_e || !ok_d) {
        ESP_LOGE(TAG, "ToF essencial faltando; abortando.");
        return;
    }
    ESP_LOGI(TAG, "ToFs OK | FRONT 0x%02X | ESQ 0x%02X | DIR 0x%02X",
             g_tof_front.address(), g_tof_esq.address(), g_tof_dir.address());

    // 3) Giroscopio.
    g_mpu_ok = init_mpu();

    // 4) Motores.
    initMotores();
    motores_para();

    // 5) Largada + calibracao do gyro (robo PARADO).
    ESP_LOGI(TAG, "Largada em %d ms... tire a mao e mantenha PARADO.", ESPERA_MS);
    vTaskDelay(pdMS_TO_TICKS(ESPERA_MS));
    if (g_mpu_ok) { ESP_LOGI(TAG, "Calibrando gyro..."); calibrar_bias_z(); }

    // Pose inicial: (0,0) apontando para o Norte.
    g_x = 0; g_y = 0; g_h = 0; g_stop = false;

    // 6) Ciclo por CELULA: olha+mapeia -> decide virada -> anda 1 tile.
    while (!g_stop) {
        decidirVirada();                 // mapeia, checa 2x2 e vira (ou segue reto)
        if (g_stop) break;

        const Avanco r = andarUmTile();  // avanca exatamente 1 celula (18 cm)
        // Atualiza a posicao no grid (1 celula no rumo atual).
        g_x += DX[g_h];
        g_y += DY[g_h];

        if (r == Avanco::PAREDE) {
            recentralizarFrontal();
        } else if (r == Avanco::SEM_REF) {
            ESP_LOGW(TAG, "Sem referencia de tile (espaco aberto).");
            vTaskDelay(pdMS_TO_TICKS(ASSENTA_MS));
        }
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    motores_para();
    ESP_LOGI(TAG, "=== FIM: 2x2 aberto encontrado. Robo parado. ===");
    while (true) vTaskDelay(portMAX_DELAY);
}
