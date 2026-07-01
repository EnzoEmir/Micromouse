/* Calibracao: PULSOS de ENCODER por 18 cm (1 tile).
 *
 * O PWM e a VELOCIDADE (duty 0..1023), nao a distancia. Quem mede o quanto o
 * robo andou e o ENCODER. Este teste descobre a constante "pulsos por mm" para
 * que a navegacao possa andar 1 tile (180 mm) contando pulsos, sem depender do
 * ToF frontal.
 *
 * Como usar:
 *   1) No main/CMakeLists.txt, deixe ativo apenas "app/teste_encoder.cpp".
 *   2) idf.py build flash monitor
 *   3) Coloque o robo numa linha de partida marcada no chao. Deixe espaco livre
 *      a frente (>= ~30 cm).
 *   4) LARGADA AUTOMATICA: apos ligar/reset ha uma contagem regressiva de
 *      ESPERA_LARGADA_S segundos (tire a mao) e o robo anda reto sozinho ate
 *      atingir CONTAGEM_ALVO pulsos (media dos 2 encoders) e PARA. Durante o
 *      trajeto ele imprime os pulsos de cada lado em tempo real.
 *   5) Meca com regua a distancia REAL percorrida (em mm) e calcule:
 *
 *        pulsos_por_mm   = CONTAGEM_ALVO / dist_real_mm
 *        PULSOS_POR_TILE = 180 mm * pulsos_por_mm
 *
 *      O proprio log ja imprime esses numeros assumindo 180 mm; basta ajustar
 *      com a distancia que voce mediu.
 *
 * Dica de precisao: quanto MAIOR a CONTAGEM_ALVO (anda mais longe), menor o erro
 * relativo da medida com regua. Comece com algo que faca o robo andar ~20-30 cm.
 *
 * Encoders (quadratura, lidos por hardware via PCNT):
 *   ESQUERDA: A=GPIO34  B=GPIO35     DIREITA: A=GPIO36(VP)  B=GPIO39(VN)
 *   ATENCAO: GPIO 34/35/36/39 sao SO-ENTRADA e NAO tem pull-up interno. O
 *   encoder precisa fornecer nivel valido (saida push-pull) ou ter pull-up
 *   externo, senao a contagem fica em zero/ruidosa.
 */

#include <cstdio>
#include <cstdlib>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pins.hpp"

static const char *TAG = "CAL_ENC";

namespace {

// =====================  PARAMETROS (CALIBRAR)  =====================

constexpr int   PWM_MAX       = 1023;                   // resolucao 10 bits
constexpr int   PWM_DRIVE     = (int)(PWM_MAX * 0.25f); // velocidade-base de avanco (25%)

// Anda RETO por encoder: corrige o PWM dos dois lados para igualar os pulsos.
constexpr float KP_RETO   = 8.0f;   // PWM por pulso de diferenca (esq-dir). CALIBRAR
constexpr int   CORR_MAX  = 200;    // saturacao da correcao de cada lado

// Calibracao de distancia (medido reto: ~1027 pulsos => 185 mm).
constexpr float PULSOS_POR_MM = 5.55f;    // 1027 / 185. RE-CALIBRAR se necessario
constexpr float TILE_MM       = 180.0f;   // 18 cm = 1 celula
// Para exatamente em 1 tile. (int) trunca; ~999 pulsos = 18 cm.
constexpr int   CONTAGEM_ALVO = (int)(PULSOS_POR_MM * TILE_MM);
constexpr int   ESPERA_LARGADA_S = 3;     // contagem regressiva antes de andar
constexpr int64_t TIMEOUT_US  = 8000000;  // trava de seguranca

// =====================  ESTADO GLOBAL  =====================

pcnt_unit_handle_t g_enc_l = nullptr;
pcnt_unit_handle_t g_enc_r = nullptr;

// =====================  ENCODER (PCNT, quadratura x4)  =====================

pcnt_unit_handle_t initEncoder(gpio_num_t a, gpio_num_t b) {
    pcnt_unit_config_t unit_cfg = {};
    unit_cfg.high_limit = 30000;
    unit_cfg.low_limit  = -30000;
    pcnt_unit_handle_t unit = nullptr;
    pcnt_new_unit(&unit_cfg, &unit);

    pcnt_glitch_filter_config_t filtro = {};
    filtro.max_glitch_ns = 1000;
    pcnt_unit_set_glitch_filter(unit, &filtro);

    pcnt_chan_config_t ca = {};
    ca.edge_gpio_num  = a;
    ca.level_gpio_num = b;
    pcnt_channel_handle_t chan_a = nullptr;
    pcnt_new_channel(unit, &ca, &chan_a);

    pcnt_chan_config_t cb = {};
    cb.edge_gpio_num  = b;
    cb.level_gpio_num = a;
    pcnt_channel_handle_t chan_b = nullptr;
    pcnt_new_channel(unit, &cb, &chan_b);

    pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                         PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                          PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                         PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                          PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    pcnt_unit_enable(unit);
    pcnt_unit_clear_count(unit);
    pcnt_unit_start(unit);
    return unit;
}

int lerEnc(pcnt_unit_handle_t unit) {
    int c = 0;
    pcnt_unit_get_count(unit, &c);
    return c;
}

// =====================  MOTORES (GPIO + LEDC direto)  =====================
// Mesma fiacao comprovada do teste_dfs (anda reto de verdade).

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

// =====================  TRAJETO DE CALIBRACAO  =====================

void andarCalibrando() {
    pcnt_unit_clear_count(g_enc_l);
    pcnt_unit_clear_count(g_enc_r);

    ESP_LOGI(TAG, ">> Andando ate media >= %d pulsos...", CONTAGEM_ALVO);
    dir_frente();
    pwm_left(PWM_DRIVE);
    pwm_right(PWM_DRIVE);

    const int64_t t0 = esp_timer_get_time();
    int el = 0, er = 0;
    int64_t t_log = 0;

    while (true) {
        // Distancia de cada lado = modulo dos pulsos (motores espelhados podem
        // contar com sinais opostos; aqui interessa o quanto cada roda girou).
        el = abs(lerEnc(g_enc_l));
        er = abs(lerEnc(g_enc_r));
        const int media = (el + er) / 2;

        // --- Anda RETO: iguala os pulsos dos dois lados ---
        // erro > 0  => esquerda andou MAIS  => robo curvou p/ DIREITA
        //           => freia a esquerda, acelera a direita.
        const int erro = el - er;
        int corr = (int)(KP_RETO * erro);
        if (corr >  CORR_MAX) corr =  CORR_MAX;
        if (corr < -CORR_MAX) corr = -CORR_MAX;

        int dl = PWM_DRIVE - corr;
        int dr = PWM_DRIVE + corr;
        if (dl < 0) dl = 0;
        if (dl > PWM_MAX) dl = PWM_MAX;
        if (dr < 0) dr = 0;
        if (dr > PWM_MAX) dr = PWM_MAX;
        pwm_left(dl);
        pwm_right(dr);

        const int64_t agora = esp_timer_get_time();
        if (agora - t_log >= 100000) {      // log a cada 100 ms
            t_log = agora;
            ESP_LOGI(TAG, "ESQ=%-6d DIR=%-6d media=%-6d erro=%-5d (pwmL=%d pwmR=%d)",
                     el, er, media, erro, dl, dr);
        }

        if (media >= CONTAGEM_ALVO) break;
        if (agora - t0 > TIMEOUT_US) {
            ESP_LOGW(TAG, "TIMEOUT antes do alvo (media=%d). Encoder ligado?", media);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    motores_para();

    el = abs(lerEnc(g_enc_l));
    er = abs(lerEnc(g_enc_r));
    const float media = (el + er) / 2.0f;
    const float ppm   = media / TILE_MM;          // SE andou exatamente 180 mm

    ESP_LOGI(TAG, "================ RESULTADO ================");
    ESP_LOGI(TAG, "Pulsos finais: ESQ=%d  DIR=%d  media=%.0f", el, er, media);
    ESP_LOGI(TAG, "MECA a distancia REAL percorrida (mm) e calcule:");
    ESP_LOGI(TAG, "  pulsos_por_mm   = %.0f / dist_real_mm", media);
    ESP_LOGI(TAG, "  PULSOS_POR_TILE = 180 * pulsos_por_mm");
    ESP_LOGI(TAG, "Se a distancia REAL = 180 mm, entao:");
    ESP_LOGI(TAG, "  pulsos_por_mm   = %.2f", ppm);
    ESP_LOGI(TAG, "  PULSOS_POR_TILE = %.0f", ppm * TILE_MM);
    ESP_LOGI(TAG, "===========================================");
}

} // namespace

extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "=== Calibracao encoder: pulsos por 18 cm ===");

    initMotores();
    motores_para();
    g_enc_l = initEncoder(MOTOR_LEFT_ENC_A_PIN,  MOTOR_LEFT_ENC_B_PIN);
    g_enc_r = initEncoder(MOTOR_RIGHT_ENC_A_PIN, MOTOR_RIGHT_ENC_B_PIN);

    // Largada automatica: contagem regressiva para voce tirar a mao e soltar o
    // robo na linha de partida.
    for (int s = ESPERA_LARGADA_S; s > 0; --s) {
        ESP_LOGI(TAG, "Largada automatica em %d s... tire a mao.", s);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    andarCalibrando();

    ESP_LOGI(TAG, "Fim. Reinicie (EN/reset) para repetir.");
    while (true) vTaskDelay(portMAX_DELAY);
}
