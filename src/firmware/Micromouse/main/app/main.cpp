//  ATENCAO: as constantes marcadas com "CALIBRAR" dependem da geometria fisica
//  do robo e PRECISAM ser ajustadas/medidas no hardware antes da primeira corrida.

#include <cstdio>
#include <cmath>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "pins.hpp"
#include "i2c_manager.hpp"
#include "battery/battery.hpp"
#include "imu.h"
#include "vl53l0x/IV_Vl53l0x.hpp"
#include "maze.hpp"
#include "telemetria.hpp"

namespace {

static const char* TAG = "MAIN";

//  Configuracao da rede / backend
const char* WIFI_SSID   = "NOME_DO_SEU_WIFI";
const char* WIFI_PASS   = "SENHA_DO_SEU_WIFI";
const char* BACKEND_URL = "http://192.168.1.50:8000/api/telemetria";

//  Constantes de movimento  (CALIBRAR NO HARDWARE)
constexpr int   PWM_MAX            = 1023;   // resolucao 10 bits (LEDC_TIMER_10_BIT)

// Quantas contagens de encoder (media das 2 rodas) equivalem a avançar uma
// celula do labirinto (~18 cm). MEDIR: ande 1 celula e leia o encoder. CALIBRAR.
constexpr int   COUNTS_PER_CELL    = 1000;

// Distancia (mm) abaixo da qual consideramos que existe parede naquela direcao.
constexpr float WALL_THRESHOLD_MM  = 120.0f;  // CALIBRAR conforme a celula
// Distancia (mm) de seguranca: para o avanco se houver parede colada na frente.
constexpr float FRONT_STOP_MM      = 55.0f;   // CALIBRAR

constexpr int   DRIVE_PWM          = PWM_MAX / 2; // velocidade de avanco
constexpr int   TURN_PWM           = PWM_MAX / 3; // velocidade de giro
constexpr float TURN_TARGET_RAD    = M_PI / 2.0f; // 90 graus por passo de giro
constexpr float KP_STRAIGHT        = 3.0f;        // ganho da correcao reta (encoder)

// Tamanho fisico aproximado de uma celula (~18 cm); usado para estimar v_med.
constexpr float CELL_SIZE_M        = 0.18f;
// Limiar de temperatura do IMU acima do qual a corrida e abortada (alerta tipo 5).
constexpr float TEMP_CRITICA_C     = 60.0f;       // CALIBRAR conforme o hardware

//  Recursos compartilhados
SemaphoreHandle_t g_i2c_mutex = nullptr;   // serializa o barramento I2C

Battery   g_battery;
Labirinto g_labirinto;
Telemetria g_telemetria(BACKEND_URL, 1500); // heartbeat de 1,5 s

// Ordem de boot em cascata
IV_Vl53l0x g_tof_f_left({
    .position = IV_Vl53l0x::Position::FRONT_LEFT,
    .address = I2C_ADDR_VL53L0X_ALT_1, .xshut_pin = TOF_FRONT_LEFT_XSHUT_PIN,
    .i2c_speed_hz = I2C_MANAGER_DEFAULT_SPEED_HZ, .timing_budget_ms = 20,
    .log_level = espp::Logger::Verbosity::WARN,
});
IV_Vl53l0x g_tof_front({
    .position = IV_Vl53l0x::Position::FRONT,
    .address = I2C_ADDR_VL53L0X_ALT_0, .xshut_pin = TOF_FRONT_XSHUT_PIN,
    .i2c_speed_hz = I2C_MANAGER_DEFAULT_SPEED_HZ, .timing_budget_ms = 20,
    .log_level = espp::Logger::Verbosity::WARN,
});
IV_Vl53l0x g_tof_f_right({
    .position = IV_Vl53l0x::Position::FRONT_RIGHT,
    .address = I2C_ADDR_VL53L0X_ALT_2, .xshut_pin = TOF_FRONT_RIGHT_XSHUT_PIN,
    .i2c_speed_hz = I2C_MANAGER_DEFAULT_SPEED_HZ, .timing_budget_ms = 20,
    .log_level = espp::Logger::Verbosity::WARN,
});
IV_Vl53l0x g_tof_left({
    .position = IV_Vl53l0x::Position::LEFT,
    .address = I2C_ADDR_VL53L0X_ALT_3, .xshut_pin = TOF_LEFT_XSHUT_PIN,
    .i2c_speed_hz = I2C_MANAGER_DEFAULT_SPEED_HZ, .timing_budget_ms = 20,
    .log_level = espp::Logger::Verbosity::WARN,
});
IV_Vl53l0x g_tof_right({
    .position = IV_Vl53l0x::Position::RIGHT,
    .address = I2C_ADDR_VL53L0X_ALT_4, .xshut_pin = TOF_RIGHT_XSHUT_PIN,
    .i2c_speed_hz = I2C_MANAGER_DEFAULT_SPEED_HZ, .timing_budget_ms = 20,
    .log_level = espp::Logger::Verbosity::WARN,
});

// Motores/encoders
pcnt_unit_handle_t s_pcnt_unit_r = nullptr;
pcnt_unit_handle_t s_pcnt_unit_l = nullptr;
volatile int32_t   s_enc_total_r = 0, s_enc_total_l = 0;
volatile int16_t   s_enc_last_r  = 0, s_enc_last_l  = 0;

//  Pose do robo no labirinto
enum Heading : uint8_t { NORTE = 0, LESTE = 1, SUL = 2, OESTE = 3 };

struct Pose {
    uint8_t x = 0;
    uint8_t y = 0;
    Heading heading = NORTE;
};
Pose g_pose;

// Deslocamento (dx, dy) de cada direcao absoluta; usado por avancarUmaCelula.
const int8_t kHeadingDx[4] = {0, +1, 0, -1};
const int8_t kHeadingDy[4] = {+1, 0, -1, 0};

// Snapshots para a telemetria (rodando em outra task).
portMUX_TYPE g_snap_mux = portMUX_INITIALIZER_UNLOCKED;
int   g_snap_soc        = 100;
float g_snap_temp       = 0.0f;

void atualizarSnapshot(int soc, float temp) {
    portENTER_CRITICAL(&g_snap_mux);
    g_snap_soc  = soc;
    g_snap_temp = temp;
    portEXIT_CRITICAL(&g_snap_mux);
}

const char* nomeDirecao(Heading h) {
    switch (h) {
        case NORTE: return "N";
        case LESTE: return "L";
        case SUL:   return "S";
        case OESTE: return "O";
    }
    return "N";
}

//  Controle raw dos motores
void pwm_left(uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
void pwm_right(uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}
void dir_left(bool frente) {
    gpio_set_level(MOTOR_LEFT_IN1_PIN, frente ? 1 : 0);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, frente ? 0 : 1);
}
void dir_right(bool frente) {
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, frente ? 1 : 0);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, frente ? 0 : 1);
}
void motores_para() {
    pwm_left(0);
    pwm_right(0);
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 0);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 0);
}

void encoder_read() {
    int raw_r = 0, raw_l = 0;
    pcnt_unit_get_count(s_pcnt_unit_r, &raw_r);
    pcnt_unit_get_count(s_pcnt_unit_l, &raw_l);
    int16_t cur_r = (int16_t)raw_r;
    int16_t cur_l = (int16_t)raw_l;
    s_enc_total_r += (cur_r - s_enc_last_r);
    s_enc_total_l += (cur_l - s_enc_last_l);
    s_enc_last_r = cur_r;
    s_enc_last_l = cur_l;
}
void encoder_reset() {
    pcnt_unit_clear_count(s_pcnt_unit_r);
    pcnt_unit_clear_count(s_pcnt_unit_l);
    s_enc_total_r = 0;
    s_enc_total_l = 0;
    s_enc_last_r  = 0;
    s_enc_last_l  = 0;
}

//  Leitura dos sensores
struct LeituraToF {
    float frente;
    float esquerda;
    float direita;
};

LeituraToF lerToFs() {
    LeituraToF l = {9999.0f, 9999.0f, 9999.0f};
    if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(150)) != pdTRUE) {
        ESP_LOGW(TAG, "lerToFs(): perdeu a vez no I2C");
        return l;
    }
    l.frente   = g_tof_front.readDistanceMm();
    l.esquerda = g_tof_left.readDistanceMm();
    l.direita  = g_tof_right.readDistanceMm();
    xSemaphoreGive(g_i2c_mutex);
    return l;
}

// Le o gyro_z (rad/s) ja sem o bias de calibracao. Retorna 0 se falhar.
float lerGyroZ(float* temp_out) {
    float gz = 0.0f;
    if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0.0f;
    if (imu_update()) {
        const DadosIMU d = imu_get_dados();
        gz = d.gyro_z;
        if (temp_out) *temp_out = d.temperatura;
    }
    xSemaphoreGive(g_i2c_mutex);
    return gz;
}

//  Primitivas de movimento

// Gira em torno do proprio eixo fechando o angulo pela integracao do gyro.
//   passos > 0  -> sentido horario  (vira a direita) em multiplos de 90 graus
//   passos < 0  -> sentido anti-horario (vira a esquerda)
void girar(int passos) {
    if (passos == 0) return;
    const bool horario = passos > 0;
    const float alvo   = std::fabs((float)passos) * TURN_TARGET_RAD;

    // Spin no proprio eixo: rodas em sentidos opostos.
    dir_left(horario);    // horario: esquerda p/ frente, direita p/ tras
    dir_right(!horario);
    pwm_left(TURN_PWM);
    pwm_right(TURN_PWM);

    float ang = 0.0f;
    int64_t t_prev = esp_timer_get_time();
    const int64_t timeout_us = 4000000; // 4 s de seguranca
    const int64_t t_start = t_prev;

    while (ang < alvo) {
        const float gz = lerGyroZ(nullptr);
        const int64_t now = esp_timer_get_time();
        const float dt = (now - t_prev) / 1e6f;
        t_prev = now;
        ang += std::fabs(gz) * dt;
        if (now - t_start > timeout_us) {
            ESP_LOGW(TAG, "girar(): timeout (ang=%.2f rad)", ang);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    motores_para();
    vTaskDelay(pdMS_TO_TICKS(120)); // assenta a inercia

    g_pose.heading = (Heading)(((int)g_pose.heading + passos + 4 * 4) % 4);
}

// Vira para uma direcao absoluta escolhendo o menor giro.
void virarPara(Heading destino) {
    int diff = ((int)destino - (int)g_pose.heading + 4) % 4; // 0..3 horario
    if (diff == 0) return;
    if (diff == 1)      girar(+1);  // direita
    else if (diff == 3) girar(-1);  // esquerda
    else                girar(+2);  // 180 graus
}

// Avanca uma celula fechando a distancia pelos encoders, com correcao reta
// proporcional. Para antes se aparecer parede colada na frente.
void avancarUmaCelula() {
    encoder_reset();
    dir_left(true);
    dir_right(true);

    while (true) {
        encoder_read();
        const int32_t avg = (s_enc_total_l + s_enc_total_r) / 2;
        if (avg >= COUNTS_PER_CELL) break;

        // Seguranca: parede colada na frente.
        const LeituraToF tof = lerToFs();
        if (tof.frente > 0.0f && tof.frente < FRONT_STOP_MM) {
            ESP_LOGW(TAG, "avancar(): parede a %.0f mm, parando", tof.frente);
            break;
        }

        // Correcao reta: equaliza as contagens das duas rodas.
        const int32_t erro = s_enc_total_l - s_enc_total_r;
        int duty_l = DRIVE_PWM - (int)(KP_STRAIGHT * erro);
        int duty_r = DRIVE_PWM + (int)(KP_STRAIGHT * erro);
        if (duty_l < 0) duty_l = 0;
        if (duty_l > PWM_MAX) duty_l = PWM_MAX;
        if (duty_r < 0) duty_r = 0;
        if (duty_r > PWM_MAX) duty_r = PWM_MAX;
        pwm_left(duty_l);
        pwm_right(duty_r);

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    motores_para();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Atualiza a pose so se realmente havia caminho a frente.
    g_pose.x = (uint8_t)(g_pose.x + kHeadingDx[g_pose.heading]);
    g_pose.y = (uint8_t)(g_pose.y + kHeadingDy[g_pose.heading]);
}

//  Tasks paralelas (bateria + telemetria)
void battery_task(void*) {
    const TickType_t delay = pdMS_TO_TICKS(500);
    while (true) {
        if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(150)) == pdTRUE) {
            g_battery.update();
            xSemaphoreGive(g_i2c_mutex);
        }
        const int soc = (int)g_battery.getSOC();
        portENTER_CRITICAL(&g_snap_mux);
        g_snap_soc = soc;
        portEXIT_CRITICAL(&g_snap_mux);
        vTaskDelay(delay);
    }
}

void telemetria_task(void*) {
    // Conecta ao Wi-Fi (bloqueante) e envia a configuracao inicial (tipo 0).
    int soc;
    portENTER_CRITICAL(&g_snap_mux);
    soc = g_snap_soc;
    portEXIT_CRITICAL(&g_snap_mux);
    g_telemetria.inicializar(WIFI_SSID, WIFI_PASS, g_labirinto, soc);

    const TickType_t delay = pdMS_TO_TICKS(500);
    while (true) {
        portENTER_CRITICAL(&g_snap_mux);
        soc = g_snap_soc;
        portEXIT_CRITICAL(&g_snap_mux);
        g_telemetria.verificar_heartbeat(soc);
        vTaskDelay(delay);
    }
}

//  Loop de navegacao (maquina de estados path-focused do Labirinto)
void navegacao_task(void*) {
    // Calibra o bias do gyro com o robo PARADO antes de comecar.
    if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        ESP_LOGI(TAG, "Calibrando gyro (mantenha o robo parado)...");
        imu_calibrar_gyro();
        xSemaphoreGive(g_i2c_mutex);
    }

    // Liga os stubs de movimento do Labirinto as primitivas do firmware. Os
    // lambdas sao sem captura -> convertem para ponteiro de funcao. A ordem do
    // enum Direcao (N=0,L=1,S=2,O=3) casa com Heading, dai o cast direto.
    Labirinto::InterfaceRobo robo;
    robo.virarPara = [](Labirinto::Direcao d) { virarPara(static_cast<Heading>(d)); };
    robo.avancar   = []() { avancarUmaCelula(); };
    g_labirinto.configurarRobo(robo);

    // Objetivo: uma celula do centro do labirinto. Largada em (0,0), heading N.
    const uint8_t n = g_labirinto.tamanho();
    const uint8_t centro = (uint8_t)(n / 2 - 1);
    g_labirinto.iniciar({0, 0}, {centro, centro});
    g_pose = {0, 0, NORTE};

    ESP_LOGI(TAG, "=== Iniciando navegacao path-focused (flood fill otimista) ===");

    // Referencias para estimar a velocidade media (pacote de fim de corrida).
    const int64_t t_corrida_us = esp_timer_get_time();
    int avancos = 0;
    auto velocidade_media = [&]() -> float {  // m/s
        const float dt_s = (esp_timer_get_time() - t_corrida_us) / 1e6f;
        return dt_s > 0.0f ? (avancos * CELL_SIZE_M) / dt_s : 0.0f;
    };

    bool terminar = false;
    int passos_bloqueado = 0;

    while (!terminar) {
        // 1. Le os ToFs e monta a leitura RELATIVA ao heading atual.
        const LeituraToF tof = lerToFs();
        Labirinto::LeituraSensores ls;
        ls.parede_frente   = tof.frente   > 0.0f && tof.frente   < WALL_THRESHOLD_MM;
        ls.parede_esquerda = tof.esquerda > 0.0f && tof.esquerda < WALL_THRESHOLD_MM;
        ls.parede_direita  = tof.direita  > 0.0f && tof.direita  < WALL_THRESHOLD_MM;

        // 2. Um passo da maquina de estados: registra paredes, decide a proxima
        //    direcao e ja executa o giro+avanco via os callbacks de movimento.
        const Labirinto::Resultado r = g_labirinto.passo(ls);

        // 3. Telemetria (tipo 1): reporta a celula recem-sensoriada.
        if (g_labirinto.sensoriou()) {
            const Labirinto::Posicao ps = g_labirinto.posicaoSensoriada();
            const uint8_t w = g_labirinto.paredes(ps);
            g_telemetria.movimento(ps.x, ps.y, w);
            ESP_LOGI(TAG, "Celula (%u,%u) head=%s | F:%.0f E:%.0f D:%.0f | walls=0x%X",
                     ps.x, ps.y, nomeDirecao(g_pose.heading),
                     tof.frente, tof.esquerda, tof.direita, w);
        }
        if (r == Labirinto::Resultado::EmProgresso) ++avancos;

        // 4. Monitoramento de temperatura do IMU (alerta tipo 5).
        float temp = g_snap_temp;
        lerGyroZ(&temp);
        atualizarSnapshot(g_snap_soc, temp);
        if (temp >= TEMP_CRITICA_C) {
            ESP_LOGE(TAG, "Temperatura critica: %.1f C. Abortando corrida.", temp);
            motores_para();
            g_telemetria.alerta_temperatura(temp);
            break;
        }

        // 5. Reage ao resultado do passo.
        switch (r) {
            case Labirinto::Resultado::AlcancouObjetivo: {
                ESP_LOGI(TAG, "*** Centro alcancado. Refinando o caminho otimo... ***");
                // Telemetria (tipo 2): rota otima atual de (0,0) ate o centro.
                Labirinto::Coordenada rota[Labirinto::kMaxCaminho];
                const uint16_t nlen = g_labirinto.rotaOtima(rota, Labirinto::kMaxCaminho);
                if (nlen > 0) g_telemetria.rota_otimizada(rota, nlen);
                passos_bloqueado = 0;
                break;
            }
            case Labirinto::Resultado::FastRunCompleto: {
                ESP_LOGI(TAG, "*** Fast run concluida. ***");
                motores_para();
                // Telemetria (tipo 2): rota otima verificada (mapa fechado).
                Labirinto::Coordenada rota[Labirinto::kMaxCaminho];
                const uint16_t nlen = g_labirinto.rotaOtima(rota, Labirinto::kMaxCaminho);
                if (nlen > 0) g_telemetria.rota_otimizada(rota, nlen);
                // Telemetria (tipo 3): fim de corrida com sucesso.
                g_telemetria.fim_corrida(true, velocidade_media(), g_snap_soc);
                terminar = true;
                break;
            }
            case Labirinto::Resultado::Bloqueado: {
                // Tolera bloqueios transitorios (recomputo de alvo); so desiste
                // se persistir por varios passos seguidos.
                if (++passos_bloqueado > 4) {
                    const Labirinto::Posicao p = g_labirinto.posicao();
                    ESP_LOGW(TAG, "Bloqueado em (%u,%u). Encerrando sem sucesso.", p.x, p.y);
                    motores_para();
                    g_telemetria.fim_corrida(false, velocidade_media(), g_snap_soc);
                    terminar = true;
                }
                break;
            }
            default:
                passos_bloqueado = 0;
                break;
        }
    }

    ESP_LOGI(TAG, "Navegacao finalizada. Robo parado.");
    while (true) vTaskDelay(portMAX_DELAY);
}

//  Inicializacao de hardware
bool initSensores() {
    // 1. Bateria (INA226) - tambem inicializa o barramento I2C compartilhado.
    //    OBS: o modulo Battery usa o INA226 em I2C_ADDR_INA226_DEFAULT (0x40).
    //    O combined_teste usava 0x44; confirmar o endereco do INA226 da PCB.
    if (!g_battery.init()) {
        ESP_LOGE(TAG, "Falha ao inicializar a bateria (INA226)");
        return false;
    }
    ESP_LOGI(TAG, "[OK] Bateria inicializada");

    // 2. ToFs: desliga todos e faz o boot em cascata (cada um assume seu addr).
    g_tof_f_left.disable();
    g_tof_front.disable();
    g_tof_f_right.disable();
    g_tof_left.disable();
    g_tof_right.disable();
    vTaskDelay(pdMS_TO_TICKS(20));

    if (!g_tof_f_left.init())  ESP_LOGE(TAG, "Falha ToF FRONT_LEFT");
    if (!g_tof_front.init())   ESP_LOGE(TAG, "Falha ToF FRONT");
    if (!g_tof_f_right.init()) ESP_LOGE(TAG, "Falha ToF FRONT_RIGHT");
    if (!g_tof_left.init())    ESP_LOGE(TAG, "Falha ToF LEFT");
    if (!g_tof_right.init())   ESP_LOGE(TAG, "Falha ToF RIGHT");
    ESP_LOGI(TAG, "[OK] ToFs inicializados");

    // 3. IMU (MPU9250).
    if (!imu_init()) {
        ESP_LOGE(TAG, "Falha ao inicializar o IMU");
        return false;
    }
    ESP_LOGI(TAG, "[OK] IMU inicializado");
    return true;
}

// Bring-up raw dos motores + encoders
bool initMotores() {
    gpio_config_t cfg = {};
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pin_bit_mask = (1ULL << MOTOR_STBY_PIN) |
                       (1ULL << MOTOR_RIGHT_IN1_PIN) | (1ULL << MOTOR_RIGHT_IN2_PIN) |
                       (1ULL << MOTOR_LEFT_IN1_PIN)  | (1ULL << MOTOR_LEFT_IN2_PIN);
    gpio_config(&cfg);
    gpio_set_level(MOTOR_STBY_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 0);
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 0);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 0);

    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.timer_num = LEDC_TIMER_0;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.freq_hz = 5000;
    t.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&t);

    ledc_channel_config_t ch_l = {};
    ch_l.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_l.channel = LEDC_CHANNEL_0;
    ch_l.timer_sel = LEDC_TIMER_0;
    ch_l.gpio_num = MOTOR_LEFT_PWM_PIN;
    ch_l.duty = 0;
    ledc_channel_config(&ch_l);

    ledc_channel_config_t ch_r = {};
    ch_r.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_r.channel = LEDC_CHANNEL_1;
    ch_r.timer_sel = LEDC_TIMER_0;
    ch_r.gpio_num = MOTOR_RIGHT_PWM_PIN;
    ch_r.duty = 0;
    ledc_channel_config(&ch_r);

    gpio_config_t enc_cfg = {};
    enc_cfg.mode = GPIO_MODE_INPUT;
    enc_cfg.pin_bit_mask = (1ULL << MOTOR_RIGHT_ENC_A_PIN) | (1ULL << MOTOR_RIGHT_ENC_B_PIN) |
                           (1ULL << MOTOR_LEFT_ENC_A_PIN)  | (1ULL << MOTOR_LEFT_ENC_B_PIN);
    enc_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&enc_cfg);

    pcnt_unit_config_t unit_cfg = {};
    unit_cfg.high_limit = 32767;
    unit_cfg.low_limit = -32768;
    pcnt_glitch_filter_config_t filter_cfg = { .max_glitch_ns = 12000 };

    // Encoder direito
    pcnt_new_unit(&unit_cfg, &s_pcnt_unit_r);
    pcnt_unit_set_glitch_filter(s_pcnt_unit_r, &filter_cfg);
    pcnt_chan_config_t chan_a_r = {};
    chan_a_r.edge_gpio_num = MOTOR_RIGHT_ENC_A_PIN;
    chan_a_r.level_gpio_num = MOTOR_RIGHT_ENC_B_PIN;
    pcnt_channel_handle_t ca_r = nullptr;
    pcnt_new_channel(s_pcnt_unit_r, &chan_a_r, &ca_r);
    pcnt_channel_set_edge_action(ca_r, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(ca_r, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_chan_config_t chan_b_r = {};
    chan_b_r.edge_gpio_num = MOTOR_RIGHT_ENC_B_PIN;
    chan_b_r.level_gpio_num = MOTOR_RIGHT_ENC_A_PIN;
    pcnt_channel_handle_t cb_r = nullptr;
    pcnt_new_channel(s_pcnt_unit_r, &chan_b_r, &cb_r);
    pcnt_channel_set_edge_action(cb_r, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(cb_r, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_unit_enable(s_pcnt_unit_r);
    pcnt_unit_clear_count(s_pcnt_unit_r);
    pcnt_unit_start(s_pcnt_unit_r);

    // Encoder esquerdo
    pcnt_new_unit(&unit_cfg, &s_pcnt_unit_l);
    pcnt_unit_set_glitch_filter(s_pcnt_unit_l, &filter_cfg);
    pcnt_chan_config_t chan_a_l = {};
    chan_a_l.edge_gpio_num = MOTOR_LEFT_ENC_A_PIN;
    chan_a_l.level_gpio_num = MOTOR_LEFT_ENC_B_PIN;
    pcnt_channel_handle_t ca_l = nullptr;
    pcnt_new_channel(s_pcnt_unit_l, &chan_a_l, &ca_l);
    pcnt_channel_set_edge_action(ca_l, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(ca_l, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_chan_config_t chan_b_l = {};
    chan_b_l.edge_gpio_num = MOTOR_LEFT_ENC_B_PIN;
    chan_b_l.level_gpio_num = MOTOR_LEFT_ENC_A_PIN;
    pcnt_channel_handle_t cb_l = nullptr;
    pcnt_new_channel(s_pcnt_unit_l, &chan_b_l, &cb_l);
    pcnt_channel_set_edge_action(cb_l, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(cb_l, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_unit_enable(s_pcnt_unit_l);
    pcnt_unit_clear_count(s_pcnt_unit_l);
    pcnt_unit_start(s_pcnt_unit_l);

    gpio_set_level(MOTOR_STBY_PIN, 1); // acorda a ponte H
    return true;
}

}

//  app_main
extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "=== Micromouse :: main de navegacao ===");

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    g_i2c_mutex = xSemaphoreCreateMutex();
    if (g_i2c_mutex == nullptr) {
        ESP_LOGE(TAG, "Falha ao criar o mutex do I2C");
        return;
    }

    if (!initSensores()) {
        ESP_LOGE(TAG, "Erro fatal na inicializacao dos sensores");
        return;
    }
    if (!initMotores()) {
        ESP_LOGE(TAG, "Erro fatal na inicializacao dos motores");
        return;
    }
    motores_para();

    g_labirinto.configurar(Labirinto::Tamanho::k16x16);

    ESP_LOGI(TAG, "Lancando tasks...");
    xTaskCreatePinnedToCore(battery_task,    "battery",    4096, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(telemetria_task, "telemetria", 8192, nullptr, 2, nullptr, 1);
    // A navegacao roda no core 0, separada das tasks de I/O do core 1.
    xTaskCreatePinnedToCore(navegacao_task,  "navegacao",  8192, nullptr, 5, nullptr, 0);

    ESP_LOGI(TAG, "=== Sistema pronto ===");
}