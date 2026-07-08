/* Arquitetura:
 *   - O modulo Labirinto (maze/maze.hpp) roda o FLOOD-FILL e a maquina de estados
 *     (Explorar -> Refinar -> Voltar -> FastRun). A cada passo ele sensoria as
 *     paredes da celula atual e chama duas primitivas do robo:
 *         virarPara(Direcao ABSOLUTA) -> alinha o robo para N/L/S/O (gira 90/180)
 *         avancar()                   -> anda EXATAMENTE 1 celula (tile)
 *   - Modo HIBRIDO dos sensores nessas primitivas:
 *         ENCODERS (PCNT) : distancia do tile (COUNTS_PER_TILE). NAO faz o reto.
 *         ToFs laterais   : POSICAO no corredor (centraliza entre as paredes).
 *         Giroscopio (MPU): RUMO durante o avanco (amortece/mantem reto, fusao
 *                           com os ToFs) e fecha o giro de 90/180 graus.
 *         ToF frontal     : parada de seguranca + recuo/recentralizacao.
 *
 * FLUXO DA COMPETICAO (UM botao, D19; ver pins.hpp). O botao de tamanho (D23)
 * esta com defeito de hardware, entao o D19 acumula as funcoes pelo tipo de
 * acionamento:
 *   1. TOQUE CURTO no D19 cicla LADO da largada (oeste/leste) + tamanho (4x4/8x8).
 *   2. SEGURAR o D19 (>=0.7 s) confirma: calibra o gyro e LARGA o mapeamento.
 *   3. Ao alcancar o centro, o robo PARA la (nao volta sozinho a largada).
 *   4. Reposicione o robo na largada apontando para o NORTE e de um TOQUE no
 *      D19: comeca a corrida rapida (fast run) com PWM_FAST.
 *
 * ONDE AJUSTAR O MOVIMENTO (bloco PARAMETROS abaixo):
 *   - Distancia por celula ....... COUNTS_PER_TILE
 *   - Angulo do giro (90 graus) .. ALVO_GRAUS + MARGEM_PARADA_GRAUS
 *   - Velocidade de avanco ....... PWM_DRIVE (exploracao) / PWM_FAST (corrida)
 *   - Forca da centralizacao ..... KP_CENTRO / CORR_MAX
 *   - Largada/objetivo do maze ... kOpcoesLargada (lado+tamanho+goal por botao)
 *
 * Sensores (mapeamento fisico real, descoberto na bancada):
 *   FRONTAL  = rotulo FRONT_LEFT  (addr 0x2B)
 *   ESQUERDA = rotulo FRONT       (addr 0x2A)
 *   DIREITA  = rotulo FRONT_RIGHT (addr 0x2C)
 *   GIRO     = MPU9250 (0x68) por I2C (gyro +-1000 dps, 32.8 LSB/dps)
 *   ENCODERS = PCNT nos pinos MOTOR_{LEFT,RIGHT}_ENC_{A,B}_PIN
 *   OBS: encoder DIREITO instavel na bancada; o codigo cai p/ ToF quando diverge.
 *
 * Distancias ToF sempre em mm REAIS (leitura bruta - bias caracterizado).
 */

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pins.hpp"
#include "i2c_manager.hpp"
#include "battery/battery.hpp"
#include "vl53l0x/IV_Vl53l0x.hpp"
#include "maze/maze.hpp"
#include "botoes/botoes.hpp"

#include "nvs_flash.h"
#include "wifi.hpp"
#include "envio_dados.hpp"

static const char *TAG = "TESTE_NAV";

namespace {

// =====================  PARAMETROS  =====================

// BANCADA: 1 = roda SO o self-test de motor (aciona esquerdo e direito
// separadamente, frente/re, com log claro) e NAO navega. Use para isolar um
// canal de motor morto vs. sensor/logica. Volte para 0 na competicao.
#define MOTOR_SELFTEST 0

constexpr int   PWM_MAX     = 1023;                  // resolucao 10 bits

// --- Avanco ---
// PWM_DRIVE = VELOCIDADE de avanco (fracao do PWM_MAX). Aumentar = mais rapido.
// PWM_FAST  = velocidade da corrida rapida (usada so na fase FastRun do maze).
constexpr int   PWM_DRIVE   = (int)(PWM_MAX * 0.22f);// 22% do PWM (exploracao)
constexpr int   PWM_FAST    = (int)(PWM_MAX * 0.30f);// 30% do PWM (corrida rapida)
// Compensacao da ASSIMETRIA dos motores (bancada: motor ESQUERDO bem mais forte
// que o DIREITO). Feed-forward aplicado ao PWM base: FREIA o lado forte
// (TRIM_L<1) e REFORCA o lado fraco (TRIM_R>1), deixando as duas rodas com
// velocidade parecida. E so o "chute inicial": a correcao em malha fechada
// (centralizacao ToF + rumo por gyro) fecha o resto. AJUSTE FINO na bancada:
//   - se ainda PUXA PARA A DIREITA (esquerdo forte): aumente TRIM_R / diminua TRIM_L.
//   - se passar a PUXAR PARA A ESQUERDA (corrigiu demais): o contrario.
// Log mostrou que, para andar RETO, a correcao precisava ficar presa em ~+25
// (puxando p/ direita) -> a base pendia p/ ESQUERDA (TRIM super-reforcava o
// direito). Reduzido o desbalanco (era 0.82/1.20). AJUSTE FINO: se voltar a
// puxar p/ direita, aumente TRIM_R / diminua TRIM_L; se puxar p/ esquerda, o contrario.
constexpr float TRIM_L      = 0.90f;                 // freia menos o motor esquerdo (forte)
constexpr float TRIM_R      = 1.10f;                 // reforca menos o motor direito (fraco)

// --- ToF frontal ---
constexpr float BIAS_FRENTE   = 69.0f;   // lida - real (caracterizado)
constexpr float FRONT_STOP_MM = 50.0f;   // para se a parede estiver a <= 5 cm
constexpr int   CONFIRMACOES  = 2;        // leituras seguidas p/ confirmar parede
// Dash da corrida rapida: a reta TERMINA numa parede, entao paramos pela PAREDE
// FRONTAL (referencia absoluta que corrige a deriva do encoder no trecho longo),
// e nao pela contagem de pulsos. Para a FRONT_DASH_STOP_MM da parede (com folga
// p/ nao bater) e comeca a desacelerar a (stop + DECEL_FRENTE_MM). CALIBRAR.
constexpr float FRONT_DASH_STOP_MM = 75.0f;
constexpr float DECEL_FRENTE_MM    = 80.0f;

// --- Recentralizacao frontal (pulsos) ---
constexpr float TARGET_MM   = 25.0f;     // distancia frontal final desejada
constexpr float TOL_MM      = 5.0f;      // faixa aceitavel
constexpr int   PWM_AJUSTE  = PWM_MAX / 3;
constexpr int   PWM_AJUSTE_L = (int)(PWM_AJUSTE * TRIM_L); // esquerdo freado
constexpr int   PWM_AJUSTE_R = (int)(PWM_AJUSTE * TRIM_R) > PWM_MAX
                                  ? PWM_MAX : (int)(PWM_AJUSTE * TRIM_R);
constexpr int   PULSO_MS    = 70;
constexpr int   ASSENTA_MS  = 250;
constexpr int   MAX_AJUSTES = 25;

// --- Folga para pivotar (evita travar a quina na parede ao girar) ---
// Antes de um giro, se houver parede logo a frente e o robo estiver perto
// demais, ele recua ate abrir esta folga (o giro e um pivo no eixo: a quina
// dianteira varre para frente e raspa se estiver colada na parede).
constexpr float PIVO_FOLGA_MM = 80.0f;

// --- ToFs laterais ---
constexpr float BIAS_ESQ = 55.0f;        // lida - real (esquerda)
constexpr float BIAS_DIR = 52.0f;        // lida - real (direita)
constexpr int   N_AMOSTRAS = 7;
constexpr float WALL_THRESHOLD_MM = 100.0f; // < isto = ha parede do lado
constexpr float PAREDE_ALVO_MM    = 50.0f;  // distancia desejada de cada parede
// Quantos ciclos de leitura lateral INVALIDA segurar o ultimo valor valido
// durante o avanco (cobre dropouts do ToF ao colar na parede). Alem disso,
// considera o lado sem parede (evita arrastar leitura obsoleta entre celulas).
constexpr int   HOLD_MAX_MISS     = 6;

// --- Distancia de 1 CELULA (medida pelos encoders, PCNT quadratura x4) ---
// COUNTS_PER_TILE = pulsos de encoder por celula. Aumentar = anda MAIS por
// celula; diminuir = anda MENOS. (~57 pulsos/cm; ~1065 ~= 18 cm nesta bancada).
// Parava ~2 cm curto do centro do tile -> +115 pulsos (950 -> 1065).
constexpr long    COUNTS_PER_TILE = 1065;
constexpr float   FRENTE_LIVRE_MM = 120.0f;   // df acima disso = frente ABERTA (sem parede)
constexpr int64_t TILE_TIMEOUT_US = 5000000;  // trava de seguranca por tile (5 s)
// Desaceleracao no fim do avanco: no ultimo trecho antes do alvo, se estiver em
// alta velocidade (corrida rapida), cai para PWM_DRIVE para nao PASSAR do tile
// nem bater na parede por inercia. So afeta quando pwm_base > PWM_DRIVE.
// (~57 pulsos/cm; 350 ~= 6 cm.) CALIBRAR: aumentar se ainda passar do tile.
constexpr long    DECEL_COUNTS    = 350;

// --- CENTRALIZACAO durante o avanco (ToF POSICAO + GIRO AMORTECE) ---
// corr>0 => acelera esquerda / freia direita => yaw para a DIREITA.
//   COM parede(s): a POSICAO manda (ToF). O robo pode precisar manter um ANGULO
//     corretivo constante para vencer um VIES lateral (motor/mecanica que puxa
//     sempre pro mesmo lado); por isso o giro entra so como AMORTECIMENTO pela
//     TAXA angular (-KD_GYRO*taxa), que NAO briga com esse angulo (a taxa e ~0 em
//     regime permanente). Assim a correcao ToF consegue segurar o angulo e vencer
//     o vies -- diferente do 'segurar rumo' integrado, que cancelava a correcao.
//   SEM parede: sem referencia de posicao -> o giro SEGURA o rumo reto do tile.
//   Encoders NAO entram na correcao (so na distancia do tile).
// AJUSTE na bancada (veja o log 'eToF/taxa/corr'):
//   - puxa SEMPRE pro mesmo lado e nao volta: e VIES -> ajuste TRIM_L/TRIM_R
//     (feed-forward) ate andar reto quase SEM correcao; so depois suba KP_CENTRO.
//   - serpenteia/oscila: aumente KD_GYRO (amortece) ou reduza KP_CENTRO/FILTRO_CORR.
//   - corrige pro LADO ERRADO (vai direto pra parede): inverta GYRO_SIGN_RIGHT.
constexpr float KP_CENTRO    = 1.30f;    // POSICAO por ToF (por mm) -- ALTO: precisa VENCER o TRIM
constexpr float KP_HEADING   = 6.0f;     // RUMO por gyro (SO quando nao ha parede; por grau)
constexpr float KD_GYRO      = 0.40f;    // AMORTECIMENTO (baixo: a ~4 Hz a taxa e ruidosa e atrapalha)
constexpr float GYRO_SIGN_RIGHT = -1.0f; // sinal p/ +taxa/+rumo = yaw DIREITA (padrao MPU: gz>0=esq)
constexpr int   CORR_MAX     = 150;      // teto da correcao (subiu p/ vencer o vies lateral)
constexpr float FILTRO_CORR  = 0.45f;    // suavizacao (subiu p/ responder mais rapido; giro amortece)
constexpr float ZONA_MORTA_MM = 8.0f;    // ignora erros laterais de POSICAO menores que isto

// --- GIRO de 90/180 graus (pivo no eixo, angulo fechado pelo giroscopio) ---
// Registradores do MPU9250 (nao mexer):
constexpr uint8_t MPU9250_ADDR        = 0x68;
constexpr uint8_t MPU_REG_PWR_MGMT_1  = 0x6B;
constexpr uint8_t MPU_REG_SMPLRT_DIV  = 0x19;
constexpr uint8_t MPU_REG_CONFIG      = 0x1A;
constexpr uint8_t MPU_REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t MPU_REG_ACCEL_CONFIG  = 0x1C;
constexpr uint8_t MPU_REG_ACCEL_CONFIG2 = 0x1D;
constexpr uint8_t MPU_REG_ACCEL_XOUT_H  = 0x3B;
constexpr float   GYRO_LSB_POR_DPS    = 32.8f;  // +-1000 dps (evita saturacao no pivo)
constexpr int     PWM_GIRO            = (int)(PWM_MAX * 0.35f); // 35% (fase rapida do giro)
constexpr int     PWM_GIRO_SLOW       = (int)(PWM_MAX * 0.22f); // 22% (fase lenta, aproximacao)
// >>> AJUSTE DO GIRO DE 90 GRAUS <<<
// ALVO_GRAUS: angulo desejado do giro (90). MARGEM_PARADA_GRAUS: quanto antes do
// alvo o motor e cortado (a inercia completa o resto). Se o robo:
//   - gira de MENOS (fecha < 90): DIMINUA MARGEM_PARADA_GRAUS.
//   - gira de MAIS  (passa de 90): AUMENTE MARGEM_PARADA_GRAUS.
constexpr float   ALVO_GRAUS          = 90.0f;  // angulo alvo do giro
constexpr float   APPROACH_GRAUS      = 35.0f;  // faltando isto p/ o alvo, entra na fase lenta
constexpr float   MARGEM_PARADA_GRAUS = 25.0f;  // corta o motor a (ALVO - isto); inercia fecha o resto
constexpr int     SETTLE_MS           = 400;
constexpr int64_t GIRO_TIMEOUT_US     = 8000000; // trava de seguranca por giro
// Correcao fina do giro (malha fechada): o corte + inercia costuma parar ANTES
// de 90 graus e de forma inconsistente (varia com bateria/atrito). Apos assentar,
// completa o giro com pulsos curtos ate entrar na tolerancia.
//   - se ainda fecha CURTO: aumente MAX_TRIMS_GIRO ou TRIM_GIRO_PULSO_MS.
//   - se passar de 90 (overshoot no trim): reduza TRIM_GIRO_PULSO_MS.
constexpr float   TOL_GIRO_GRAUS      = 3.0f;   // aceita 90 +/- isto
constexpr int     TRIM_GIRO_PULSO_MS  = 60;     // duracao de cada pulso de correcao
constexpr int     TRIM_GIRO_SETTLE_MS = 120;    // assentamento/medida entre pulsos
constexpr int     MAX_TRIMS_GIRO      = 12;     // teto de pulsos (trava de seguranca)

// --- Labirinto (flood-fill) ---
// O botao (D19) cicla as combinacoes LADO_DE_LARGADA + TAMANHO abaixo. O NORTE
// do modelo e SEMPRE a frente do robo na largada (o Labirinto forca heading=Norte),
// entao a ORIENTACAO inicial nao muda entre as opcoes -- muda so a CELULA:
//   ESQUERDA (Oeste): canto oeste, x = 0.
//   DIREITA  (Leste): canto leste, x = n-1.
// Em ambos y = 0 (fileira de tras) e a frente aponta para o centro (+y). O GOAL
// (uma celula do bloco central 2x2) acompanha o tamanho: 4x4 -> {1,1}; 8x8 -> {3,3}.
enum class LadoLargada : uint8_t { Oeste, Leste };

struct OpcaoLargada {
    Labirinto::Tamanho tamanho;
    LadoLargada        lado;
    Labirinto::Posicao goal;
};
constexpr OpcaoLargada kOpcoesLargada[] = {
    {Labirinto::Tamanho::k4x4, LadoLargada::Oeste, {1, 1}},
    {Labirinto::Tamanho::k4x4, LadoLargada::Leste, {1, 1}},
    {Labirinto::Tamanho::k8x8, LadoLargada::Oeste, {3, 3}},
    {Labirinto::Tamanho::k8x8, LadoLargada::Leste, {3, 3}},
};
constexpr int kNumOpcoesLargada = sizeof(kOpcoesLargada) / sizeof(kOpcoesLargada[0]);

// Celula de largada de uma opcao: x = 0 no oeste, x = n-1 no leste; y = 0 sempre.
inline Labirinto::Posicao celulaLargada(const OpcaoLargada &o) {
    const uint8_t n = static_cast<uint8_t>(o.tamanho);
    const uint8_t x = (o.lado == LadoLargada::Oeste) ? 0 : static_cast<uint8_t>(n - 1);
    return {x, 0};
}
inline const char *nomeLado(LadoLargada l) {
    return (l == LadoLargada::Oeste) ? "ESQUERDA (Oeste)" : "DIREITA (Leste)";
}
// Valor do lado da largada para o JSON de telemetria (pacote tipo 0).
inline const char *ladoJson(LadoLargada l) {
    return (l == LadoLargada::Oeste) ? "esquerda" : "direita";
}

// --- Telemetria / Wi-Fi (envio para o servidor web) ---
// >>> PREENCHER NO DIA DA COMPETICAO: SSID/senha do hotspot e IP do servidor <<<
const char* WIFI_SSID   = "dudaa28-Latitude-3420";
const char* WIFI_PASS   = "ck0fQGxy";
const char* BACKEND_URL = "http://10.42.0.1:8000/api/telemetria/pacote";
constexpr int   HEARTBEAT_MS   = 1500;   // periodo do heartbeat (tipo 4)
constexpr float TEMP_CRITICA_C = 60.0f;  // limiar do alerta critico (tipo 5)
constexpr float TILE_M         = 0.18f;  // 1 tile = 18 cm (ver COUNTS_PER_TILE)
// Temperatura vem do proprio MPU9250 (registrador TEMP_OUT), sem sensor extra.
constexpr uint8_t MPU_REG_TEMP_OUT_H = 0x41;
constexpr float   TEMP_SENS_LSB_C    = 333.87f; // sensibilidade (LSB/C) do MPU9250
constexpr float   TEMP_OFFSET_C      = 21.0f;   // offset do sensor

// =====================  ESTADO GLOBAL  =====================

Battery g_battery;
i2c_master_dev_handle_t g_mpu = nullptr;
bool  g_mpu_ok = false;
float g_bias_z = 0.0f;

Labirinto g_maze;
Labirinto::Direcao g_heading = Labirinto::Direcao::Norte; // orientacao FISICA

// --- Telemetria ---
// O Wi-Fi conecta em BACKGROUND (wifi_init_sta nao-bloqueante); cada envio
// consulta wifi_is_connected() na hora. Se a conexao chegar depois da largada,
// a task de heartbeat envia a config inicial (tipo 0) atrasada.
int64_t g_t0_ms         = 0;           // instante de inicio da corrida (ms)
volatile int   g_bateria_pct  = 100;   // cache da bateria (%), atualizado pela nav
volatile float g_temp_c       = 25.0f; // cache da temperatura do MPU (C)
volatile bool  g_temp_critica = false; // ficou acima do limiar critico
volatile bool  g_config_enviada = false; // pacote 0 (config) ja foi enviado
volatile int   g_dimensao     = 4;     // tamanho escolhido (p/ config atrasada)
const char*    g_lado_json    = "esquerda"; // lado da largada p/ config (tipo 0)
int     g_fastrun_tiles = 0;           // tiles andados durante a corrida rapida
int64_t g_fastrun_t0_ms = 0;           // inicio da corrida rapida (ms)

// Encoders (PCNT): uma unidade por roda.
pcnt_unit_handle_t g_enc_left  = nullptr;
pcnt_unit_handle_t g_enc_right = nullptr;

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
    mpu_write(MPU_REG_GYRO_CONFIG,   0x10); // +-1000 dps (evita saturacao no pivo rapido)
    mpu_write(MPU_REG_ACCEL_CONFIG,  0x00);
    mpu_write(MPU_REG_ACCEL_CONFIG2, 0x03);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "MPU9250 OK (gyro +-1000 dps).");
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

// =====================  ENCODERS (PCNT)  =====================

// Cria uma unidade PCNT em quadratura x4 para um encoder (mesma config do
// modulo motor/motor.cpp, que ja foi validado na bancada).
bool init_encoder(pcnt_unit_handle_t *unit, gpio_num_t a_pin, gpio_num_t b_pin) {
    pcnt_unit_config_t unit_config = {};
    unit_config.high_limit = 30000;
    unit_config.low_limit  = -30000;
    if (pcnt_new_unit(&unit_config, unit) != ESP_OK) return false;

    pcnt_glitch_filter_config_t filter_config = {};
    filter_config.max_glitch_ns = 1000;
    pcnt_unit_set_glitch_filter(*unit, &filter_config);

    pcnt_chan_config_t chan_a_config = {};
    chan_a_config.edge_gpio_num  = a_pin;
    chan_a_config.level_gpio_num = b_pin;
    pcnt_channel_handle_t chan_a = nullptr;
    pcnt_new_channel(*unit, &chan_a_config, &chan_a);

    pcnt_chan_config_t chan_b_config = {};
    chan_b_config.edge_gpio_num  = b_pin;
    chan_b_config.level_gpio_num = a_pin;
    pcnt_channel_handle_t chan_b = nullptr;
    pcnt_new_channel(*unit, &chan_b_config, &chan_b);

    pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                 PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                  PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                 PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                  PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    pcnt_unit_enable(*unit);
    pcnt_unit_clear_count(*unit);
    pcnt_unit_start(*unit);
    return true;
}
bool initEncoders() {
    bool ok_l = init_encoder(&g_enc_left,  MOTOR_LEFT_ENC_A_PIN,  MOTOR_LEFT_ENC_B_PIN);
    bool ok_r = init_encoder(&g_enc_right, MOTOR_RIGHT_ENC_A_PIN, MOTOR_RIGHT_ENC_B_PIN);
    if (!ok_l) ESP_LOGE(TAG, "Falha encoder ESQUERDO");
    if (!ok_r) ESP_LOGE(TAG, "Falha encoder DIREITO");
    return ok_l && ok_r;
}
long enc_left()  { int c = 0; pcnt_unit_get_count(g_enc_left,  &c); return c; }
long enc_right() { int c = 0; pcnt_unit_get_count(g_enc_right, &c); return c; }
void enc_zerar() {
    pcnt_unit_clear_count(g_enc_left);
    pcnt_unit_clear_count(g_enc_right);
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

// Le um ToF e devolve a distancia REAL (mm). <0 se invalida/fora de alcance.
float lerReal(IV_Vl53l0x &tof, float bias) {
    const float bruto = tof.readDistanceMm();
    if (bruto <= 0.0f || bruto >= 9000.0f) return -1.0f;
    float real = bruto - bias;
    if (real < 0.0f) real = 0.0f;
    return real;
}
// Mediana de n leituras (para usar com o robo PARADO). <0 se nada valido.
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

// =====================  TELEMETRIA (envio para o web)  =====================

// timestamp_ms relativo ao inicio da corrida, como pede a especificacao.
int64_t telemetria_ts() { return esp_timer_get_time() / 1000 - g_t0_ms; }

// NVS e requisito do esp_wifi_init (padrao erase-e-retry).
void inicializar_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

// Le a bateria (INA226 via I2C) e atualiza o cache. SO a task de navegacao
// chama isto -> o barramento I2C fica com um unico dono.
int atualizar_bateria() {
    g_battery.update();
    float soc = g_battery.getSOC();
    if (soc < 0.0f)   soc = 0.0f;
    if (soc > 100.0f) soc = 100.0f;
    g_bateria_pct = (int)(soc + 0.5f);

    static int64_t t_log_ina = 0;
    const int64_t now = esp_timer_get_time();
    if (now - t_log_ina >= 2000000) {
        t_log_ina = now;
        const float v = g_battery.getVoltage();
        ESP_LOGI(TAG, "INA226: V=%.2f V | I=%.3f A | P=%.2f W | SOC=%d%%",
                 v, g_battery.getCurrent(), g_battery.getPower(), g_bateria_pct);
        if (v < 1.0f) {
            ESP_LOGW(TAG, "INA226: tensao ~0 V; verifique o barramento/conexoes.");
        }
    }
    return g_bateria_pct;
}

// Le a temperatura do MPU9250 (TEMP_OUT) e marca o flag se passar do limiar.
void atualizar_temp() {
    if (!g_mpu_ok) return;
    uint8_t d[2] = {0};
    if (mpu_read(MPU_REG_TEMP_OUT_H, d, sizeof(d)) != ESP_OK) return;
    const int16_t traw = be_i16(d);
    g_temp_c = traw / TEMP_SENS_LSB_C + TEMP_OFFSET_C;
    if (g_temp_c >= TEMP_CRITICA_C) g_temp_critica = true;
}

// Task de fundo: mantem a conexao viva com heartbeats (tipo 4). SO faz HTTP
// (le globais em cache) -> nao toca no I2C, nao concorre com a navegacao.
void tarefa_heartbeat(void*) {
    while (true) {
        if (wifi_is_connected()) {
            // Wi-Fi conectou depois da largada: manda a config (tipo 0)
            // atrasada antes do primeiro heartbeat.
            if (!g_config_enviada) {
                g_config_enviada = true;
                enviar_configuracao_inicial(BACKEND_URL, telemetria_ts(),
                                            g_dimensao, g_lado_json, g_bateria_pct);
            }
            enviar_heartbeat(BACKEND_URL, telemetria_ts(), g_bateria_pct);
        }
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_MS));
    }
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
    bool lento   = false;  // ja passou para a fase lenta de aproximacao

    // Integra a velocidade angular do gyro por `ms` (mantendo t_prev continuo).
    // Reusada na medida de assentamento e na correcao fina em malha fechada.
    auto integrar_por = [&](int ms) {
        const int64_t tstart = esp_timer_get_time();
        while (esp_timer_get_time() - tstart < (int64_t)ms * 1000) {
            const int64_t now = esp_timer_get_time();
            float dt = (now - t_prev) / 1e6f;
            if (dt <= 0.0f) dt = 1e-3f;
            t_prev = now;
            float gz;
            if (ler_gz_dps(&gz)) angulo += gz * dt;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    };

    // Fase 1: gira ate o alvo (com margem) ou timeout, desacelerando na chegada.
    while (fabsf(angulo) < ALVO_GRAUS - MARGEM_PARADA_GRAUS) {
        if (esp_timer_get_time() - t0 > GIRO_TIMEOUT_US) { timeout = true; break; }
        const int64_t now = esp_timer_get_time();
        float dt = (now - t_prev) / 1e6f;
        if (dt <= 0.0f) dt = 1e-3f;
        t_prev = now;
        float gz;
        if (ler_gz_dps(&gz)) angulo += gz * dt;

        // Desacelera perto do alvo: menos inercia ao parar = menos overshoot.
        if (!lento && fabsf(angulo) >= ALVO_GRAUS - APPROACH_GRAUS) {
            lento = true;
            pwm_left(PWM_GIRO_SLOW); pwm_right(PWM_GIRO_SLOW);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    motores_para();

    // Fase 2: mede o angulo final durante a parada (inclui inercia).
    integrar_por(SETTLE_MS);

    // Fase 3: correcao fina em malha fechada. Enquanto o giro estiver CURTO
    // (fora da tolerancia), aplica pulsos curtos no mesmo sentido ate fechar os
    // 90 graus. Elimina o giro-de-menos e a inconsistencia do corte+inercia.
    int trims = 0;
    while (!timeout && fabsf(angulo) < ALVO_GRAUS - TOL_GIRO_GRAUS && trims < MAX_TRIMS_GIRO) {
        ++trims;
        if (sentido_direita) dir_girar_direita(); else dir_girar_esquerda();
        pwm_left(PWM_GIRO_SLOW); pwm_right(PWM_GIRO_SLOW);
        integrar_por(TRIM_GIRO_PULSO_MS);
        motores_para();
        integrar_por(TRIM_GIRO_SETTLE_MS); // deixa assentar e mede antes do proximo pulso
        ESP_LOGI(TAG, "   trim giro %d: angulo=%.1f graus", trims, fabsf(angulo));
    }

    if (timeout) ESP_LOGW(TAG, "<< TIMEOUT giro %s. Angulo=%.1f", nome, fabsf(angulo));
    else ESP_LOGI(TAG, "<< Giro %s ok. Angulo final=%.1f graus (%d trim)",
                  nome, fabsf(angulo), trims);
}

// =====================  PRIMITIVAS DE MOVIMENTO  =====================

// Resultado de tentar avancar 1 celula.
enum class Avanco {
    TILE_OK,   // avancou 1 celula
    PAREDE,    // encostou numa parede antes de completar a celula
};

// PWM base do avanco: PWM_FAST na corrida rapida (fase FastRun do maze),
// PWM_DRIVE no resto (exploracao/refino/retorno).
int pwmAvanco() {
    return (g_maze.fase() == Labirinto::Fase::FastRun) ? PWM_FAST : PWM_DRIVE;
}

// Anda n_tiles tiles contando PULSOS de encoder, SEM parar entre eles. Mantem
// linha reta pela diferenca L/R dos encoders e adiciona centralizacao ToF suave
// (hibrido). Desacelera no fim para nao passar do tile em alta velocidade.
//   parar_frente_mm >= 0: modo DASH (reta que termina em parede). A parada
//   PRIMARIA passa a ser a PAREDE FRONTAL a essa distancia (referencia absoluta
//   que corrige a deriva do encoder no trecho longo); a contagem de pulsos vira
//   apenas o teto de seguranca. Padrao (-1) para 1 tile pela contagem, com o ToF
//   frontal so como parada de seguranca (FRONT_STOP_MM).
Avanco andarUmTile(int n_tiles = 1, float parar_frente_mm = -1.0f) {
    if (n_tiles < 1) n_tiles = 1;
    const bool para_frente = (parar_frente_mm >= 0.0f);
    // Teto de pulsos. No modo dash, meio tile alem (a parada real e a parede);
    // se por algum motivo a parede nao for vista, o encoder segura o avanco.
    const long alvo = (long)COUNTS_PER_TILE * n_tiles +
                      (para_frente ? COUNTS_PER_TILE / 2 : 0);
    const float stop_frente = para_frente ? parar_frente_mm : FRONT_STOP_MM;
    const int pwm_base   = pwmAvanco();
    enc_zerar();
    dir_frente();
    const int64_t t0 = esp_timer_get_time();
    int64_t t_log     = t0;   // throttle dos logs de debug
    int   confirmados = 0;
    float corr_filt   = 0.0f;
    bool  aviso_enc   = false; // avisa 1x se detectar encoder morto
    // Ultima leitura lateral VALIDA neste tile. O VL53L0X perde a medida de
    // forma intermitente quando o robo esta MUITO perto da parede (abaixo da
    // faixa util do sensor); sem isso a parede "some" e a centralizacao para de
    // corrigir. Segura o ultimo valor valido ate chegar outra leitura (perto ou
    // longe), entao qualquer leitura valida atualiza e ele se auto-corrige.
    float de_hold = -1.0f;
    float dd_hold = -1.0f;
    int   de_miss = 0;   // ciclos invalidos seguidos na leitura esquerda
    int   dd_miss = 0;   // ciclos invalidos seguidos na leitura direita
    // Rumo integrado pelo giroscopio NESTE tile (graus), referencia = 0 na
    // entrada. Usado para amortecer a correcao e manter o robo paralelo.
    float   ang_tile = 0.0f;
    int64_t t_gyro   = esp_timer_get_time();
    ESP_LOGI(TAG, ">> Tile: alvo=%ld pulsos (%d tile%s)", alvo, n_tiles,
             n_tiles > 1 ? "s" : "");

    while (true) {
        const long cl = std::labs(enc_left());
        const long cr = std::labs(enc_right());

        // Robustez a encoder atrasado/intermitente na DISTANCIA: se as contagens
        // divergem muito, o lado que conta MENOS esta falhando -> mede pelo MAIOR.
        // Se estao proximas, usa a media (comportamento normal). O rumo/reto NAO
        // depende mais do encoder (agora e gyro), so a distancia do tile.
        const long maxc = std::max(cl, cr);
        const long minc = std::min(cl, cr);
        const bool enc_diverge = (maxc > 150 && minc < (maxc * 3) / 5);
        const long media = enc_diverge ? maxc : (cl + cr) / 2;
        if (enc_diverge && !aviso_enc) {
            aviso_enc = true;
            ESP_LOGW(TAG, "Encoders divergindo (cl=%ld cr=%ld): medindo distancia "
                          "pelo maior. Verifique o encoder %s.",
                     cl, cr, (cl < cr) ? "ESQUERDO" : "DIREITO");
        }

        // (a) Parada pela parede frontal. No modo dash e a parada PRIMARIA
        // (stop_frente = FRONT_DASH_STOP_MM, referencia absoluta); caso contrario
        // e so a parada de seguranca (stop_frente = FRONT_STOP_MM).
        const float df = lerReal(g_tof_front, BIAS_FRENTE);
        if (df >= 0.0f && df <= stop_frente) {
            if (++confirmados >= CONFIRMACOES) {
                motores_para();
                ESP_LOGW(TAG, "<< Tile PAREDE: df=%.0f mm (<=%.0f) | encL=%ld encR=%ld media=%ld",
                         df, stop_frente, cl, cr, media);
                return Avanco::PAREDE;
            }
        } else {
            confirmados = 0;
        }

        // (b) Completou o(s) tile(s) pela contagem de pulsos.
        if (media >= alvo) {
            motores_para();
            ESP_LOGI(TAG, "<< Tile OK: encL=%ld encR=%ld media=%ld (dif L/R=%ld)",
                     cl, cr, media, cr - cl);
            return Avanco::TILE_OK;
        }

        // (c) Correcao de centralizacao: FUSAO ToF (posicao) + gyro (rumo).
        const float de_raw = lerReal(g_tof_esq, BIAS_ESQ);
        const float dd_raw = lerReal(g_tof_dir, BIAS_DIR);
        // Memoriza a ultima leitura valida; se ficar invalida por muitos ciclos
        // seguidos, descarta (o lado passou a ser considerado sem parede).
        if (de_raw >= 0.0f)            { de_hold = de_raw; de_miss = 0; }
        else if (++de_miss > HOLD_MAX_MISS) de_hold = -1.0f;
        if (dd_raw >= 0.0f)            { dd_hold = dd_raw; dd_miss = 0; }
        else if (++dd_miss > HOLD_MAX_MISS) dd_hold = -1.0f;
        const float de = de_hold;             // usa a memoria (aguenta dropouts)
        const float dd = dd_hold;
        const bool  we = (de >= 0.0f && de < WALL_THRESHOLD_MM);
        const bool  wd = (dd >= 0.0f && dd < WALL_THRESHOLD_MM);
        float err_tof = 0.0f;
        if (we && wd)      err_tof = dd - de;              // centra entre 2 paredes
        else if (we)       err_tof = PAREDE_ALVO_MM - de;  // segue parede esquerda
        else if (wd)       err_tof = dd - PAREDE_ALVO_MM;  // segue parede direita
        if (std::fabs(err_tof) < ZONA_MORTA_MM) err_tof = 0.0f;

        // Giroscopio: taxa angular (dps) e rumo integrado no tile. Se o gyro
        // falhou (g_mpu_ok=false), ambos ficam ~0 e cai para so-ToF.
        float gz_dps = 0.0f;
        if (g_mpu_ok) ler_gz_dps(&gz_dps);
        const int64_t now_g = esp_timer_get_time();
        float dt_g = (now_g - t_gyro) / 1e6f;
        if (dt_g <= 0.0f) dt_g = 1e-3f;
        if (dt_g > 0.1f)  dt_g = 0.1f;      // ignora saltos (nao integra lixo)
        t_gyro = now_g;
        ang_tile += gz_dps * dt_g;
        const float taxa  = GYRO_SIGN_RIGHT * gz_dps;   // >0 = girando p/ DIREITA
        const float rumo  = GYRO_SIGN_RIGHT * ang_tile; // >0 = ja girou p/ DIREITA

        // COM parede: ToF define a posicao, giro so AMORTECE (taxa). Assim a
        // correcao pode manter um angulo corretivo e vencer o vies lateral.
        // SEM parede: sem posicao -> segura o rumo reto do tile pelo giro.
        float corr;
        if (we || wd) corr = KP_CENTRO * err_tof - KD_GYRO * taxa;
        else          corr = -KP_HEADING * rumo    - KD_GYRO * taxa;
        if (corr >  CORR_MAX) corr =  CORR_MAX;
        if (corr < -CORR_MAX) corr = -CORR_MAX;
        corr_filt += FILTRO_CORR * (corr - corr_filt);

        // Desacelera no fim do trecho: em alta velocidade (fast run) cai para
        // PWM_DRIVE, para o coast final ser o mesmo da exploracao (onde
        // COUNTS_PER_TILE foi calibrado) -> nao passa do tile nem bate na parede.
        // Gatilho por CONTAGEM (no modo normal) ou pela DISTANCIA FRONTAL (dash,
        // onde a parada e a parede). Sem efeito na exploracao (pwm_base=PWM_DRIVE).
        const bool perto_fim    = (alvo - media) < DECEL_COUNTS;
        const bool perto_frente = para_frente && df >= 0.0f &&
                                  df < stop_frente + DECEL_FRENTE_MM;
        int pwm_eff = pwm_base;
        if (pwm_base > PWM_DRIVE && (perto_fim || perto_frente)) pwm_eff = PWM_DRIVE;
        const int pwm_l = std::min(PWM_MAX, (int)(pwm_eff * TRIM_L));
        const int pwm_r = std::min(PWM_MAX, (int)(pwm_eff * TRIM_R));

        int dl = pwm_l + (int)corr_filt; // corr>0 => acelera esquerda
        int dr = pwm_r - (int)corr_filt;
        dl = std::max(0, std::min(dl, PWM_MAX));
        dr = std::max(0, std::min(dr, PWM_MAX));
        pwm_left(dl); pwm_right(dr);

        // Debug throttled (~250 ms): progresso, erros e PWM aplicado.
        const int64_t now = esp_timer_get_time();
        if (now - t_log >= 250000) {
            t_log = now;
            ESP_LOGI(TAG,
                     "  tile: media=%ld/%ld | de=%.0f dd=%.0f par=%c%c | eToF=%+.0f"
                     " taxa=%+.0f rumo=%+.1f corr=%+d | PWM L=%d R=%d",
                     media, alvo, de, dd, we ? 'E' : '-', wd ? 'D' : '-',
                     err_tof, taxa, rumo, (int)corr_filt, dl, dr);
        }

        // (d) Trava de seguranca por celula (escala com o numero de tiles).
        if (esp_timer_get_time() - t0 > TILE_TIMEOUT_US * n_tiles) {
            motores_para();
            ESP_LOGW(TAG, "Tile: timeout (media=%ld pulsos).", media);
            return Avanco::TILE_OK;
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
        const float err = d - TARGET_MM; // >0 longe; <0 perto
        if (std::fabs(err) <= TOL_MM) { ESP_LOGI(TAG, "frontal ok: %.0f mm", d); return; }

        if (err < 0.0f) dir_re(); else dir_frente();
        pwm_left(PWM_AJUSTE_L); pwm_right(PWM_AJUSTE_R);
        vTaskDelay(pdMS_TO_TICKS(PULSO_MS));
        motores_para();
        vTaskDelay(pdMS_TO_TICKS(ASSENTA_MS));
        ESP_LOGI(TAG, "ajuste %d: d=%.0f err=%+.0f -> %s", i + 1, d, err,
                 err < 0.0f ? "RE" : "frente");
    }
    ESP_LOGW(TAG, "ajuste frontal: nao convergiu");
}

// Abre folga frontal antes de um pivo: se ha parede a frente e o robo esta perto
// demais para girar sem raspar, recua (pelo ToF frontal) ate PIVO_FOLGA_MM. Nao
// faz nada se a frente estiver aberta (leitura invalida ou ja com folga).
void folgaParaPivo() {
    for (int i = 0; i < MAX_AJUSTES; ++i) {
        const float df = lerEstavel(g_tof_front, BIAS_FRENTE, 5);
        if (df < 0.0f)              return;   // sem parede/leitura: nada a fazer
        if (df >= PIVO_FOLGA_MM)    return;   // ja tem folga suficiente
        dir_re();
        pwm_left(PWM_AJUSTE_L); pwm_right(PWM_AJUSTE_R);
        vTaskDelay(pdMS_TO_TICKS(PULSO_MS));
        motores_para();
        vTaskDelay(pdMS_TO_TICKS(ASSENTA_MS));
        ESP_LOGI(TAG, "folga p/ pivo: df=%.0f mm -> recuando (alvo %.0f)", df, PIVO_FOLGA_MM);
    }
    ESP_LOGW(TAG, "folga p/ pivo: nao atingiu %.0f mm", PIVO_FOLGA_MM);
}

// =====================  INTERFACE PARA O LABIRINTO  =====================

// Le as 3 paredes da celula atual (robo PARADO), em relacao ao robo.
Labirinto::LeituraSensores lerParedes() {
    const float de = lerEstavel(g_tof_esq,   BIAS_ESQ,    N_AMOSTRAS);
    const float dd = lerEstavel(g_tof_dir,   BIAS_DIR,    N_AMOSTRAS);
    const float df = lerEstavel(g_tof_front, BIAS_FRENTE, 5);

    Labirinto::LeituraSensores s;
    s.parede_esquerda = (de >= 0.0f && de < WALL_THRESHOLD_MM);
    s.parede_direita  = (dd >= 0.0f && dd < WALL_THRESHOLD_MM);
    s.parede_frente   = (df >= 0.0f && df <= FRENTE_LIVRE_MM);
    ESP_LOGI(TAG, "Paredes | ESQ %s (%.0f) | FRENTE %s (%.0f) | DIR %s (%.0f)",
             s.parede_esquerda ? "PAREDE" : "livre", de,
             s.parede_frente   ? "PAREDE" : "livre", df,
             s.parede_direita  ? "PAREDE" : "livre", dd);
    return s;
}

// Callback do Labirinto: alinha o robo para a direcao ABSOLUTA `destino`,
// girando pelo caminho mais curto a partir da orientacao fisica atual.
void virarPara(Labirinto::Direcao destino) {
    static const char *kNome[4] = {"N", "L", "S", "O"};
    const int diff = ((int)destino - (int)g_heading + 4) & 0x03;
    const char *acao = (diff == 0) ? "reto"
                     : (diff == 1) ? "vira DIR 90"
                     : (diff == 2) ? "vira 180"
                                   : "vira ESQ 90";
    ESP_LOGI(TAG, "virarPara: %s -> %s (%s)",
             kNome[(int)g_heading & 3], kNome[(int)destino & 3], acao);
    switch (diff) {
        case 0: /* ja alinhado */                       break;
        case 1: girar(/*direita=*/true);                break; // 90 CW
        // 180 (beco): abre folga frontal antes, senao a quina trava na parede.
        case 2: folgaParaPivo(); girar(true); girar(true); break;
        case 3: girar(/*direita=*/false);               break; // 90 CCW
    }
    g_heading = destino;
}

// Callback do Labirinto: anda exatamente 1 celula. Se encostar numa parede,
// recentraliza a distancia frontal.
void avancar() {
    const Avanco r = andarUmTile();
    if (r == Avanco::PAREDE) {
        ESP_LOGW(TAG, "avancar: encostou numa parede; recentralizando.");
        recentralizarFrontal();
    }
    // Conta tiles da corrida rapida para estimar a velocidade media (tipo 3).
    if (r == Avanco::TILE_OK && g_maze.fase() == Labirinto::Fase::FastRun) {
        ++g_fastrun_tiles;
    }
    vTaskDelay(pdMS_TO_TICKS(120)); // pequena pausa antes de sensoriar/girar
}

// Atualiza os caches (bateria/temperatura) e checa o alerta critico. Se a
// temperatura estourou o limiar: para os motores, envia os pacotes 5 (alerta)
// e 3 (fim sem sucesso) e retorna true -> abortar a corrida.
bool abortarPorTemperatura() {
    atualizar_bateria();
    atualizar_temp();
    if (!g_temp_critica) return false;
    motores_para();
    ESP_LOGE(TAG, "TEMP CRITICA %.1f C -> abortando corrida.", g_temp_c);
    if (wifi_is_connected()) {
        enviar_alerta_temperatura(BACKEND_URL, telemetria_ts(), g_temp_c);          // tipo 5
        enviar_fim_corrida(BACKEND_URL, telemetria_ts(), false, 0.0f, g_bateria_pct); // tipo 3
    }
    return true;
}

const char *nome_resultado(Labirinto::Resultado r) {
    switch (r) {
        case Labirinto::Resultado::EmProgresso:      return "EmProgresso";
        case Labirinto::Resultado::AlcancouObjetivo: return "AlcancouObjetivo";
        case Labirinto::Resultado::CaminhoFechado:   return "CaminhoFechado";
        case Labirinto::Resultado::RetornouAoInicio: return "RetornouAoInicio";
        case Labirinto::Resultado::FastRunCompleto:  return "FastRunCompleto";
        case Labirinto::Resultado::Bloqueado:        return "Bloqueado";
        default:                                     return "?";
    }
}

#if MOTOR_SELFTEST
// Bancada: aciona UM motor de cada vez (frente/re), independente de
// sensores/gyro/navegacao, para isolar canal esquerdo vs. direito. Enquanto um
// lado gira, meca com o multimetro: PWM_PIN deve ter onda ~40%, um dos IN em
// 3V3 e o outro em 0 V, e a saida da ponte H deve ter tensao diferencial. Se os
// GPIOs chaveiam certo mas o motor nao gira -> hardware (ponte H / solda / fio).
// Nunca retorna.
void motorSelfTest() {
    initMotores();
    motores_para();
    const int pwm = (int)(PWM_MAX * 0.40f);
    ESP_LOGW(TAG, "=== MOTOR SELFTEST === ESQ: PWM=GPIO%d IN1=GPIO%d IN2=GPIO%d | "
                  "DIR: PWM=GPIO%d IN1=GPIO%d IN2=GPIO%d",
             (int)MOTOR_LEFT_PWM_PIN,  (int)MOTOR_LEFT_IN1_PIN,  (int)MOTOR_LEFT_IN2_PIN,
             (int)MOTOR_RIGHT_PWM_PIN, (int)MOTOR_RIGHT_IN1_PIN, (int)MOTOR_RIGHT_IN2_PIN);

    while (true) {
        ESP_LOGW(TAG, "[SELFTEST] ESQUERDO frente (PWM %d)", pwm);
        dir_frente(); pwm_right(0); pwm_left(pwm);
        vTaskDelay(pdMS_TO_TICKS(1500));
        motores_para(); vTaskDelay(pdMS_TO_TICKS(500));

        ESP_LOGW(TAG, "[SELFTEST] ESQUERDO re     (PWM %d)", pwm);
        dir_re(); pwm_right(0); pwm_left(pwm);
        vTaskDelay(pdMS_TO_TICKS(1500));
        motores_para(); vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGW(TAG, "[SELFTEST] DIREITO  frente (PWM %d)", pwm);
        dir_frente(); pwm_left(0); pwm_right(pwm);
        vTaskDelay(pdMS_TO_TICKS(1500));
        motores_para(); vTaskDelay(pdMS_TO_TICKS(500));

        ESP_LOGW(TAG, "[SELFTEST] DIREITO  re     (PWM %d)", pwm);
        dir_re(); pwm_left(0); pwm_right(pwm);
        vTaskDelay(pdMS_TO_TICKS(1500));
        motores_para(); vTaskDelay(pdMS_TO_TICKS(1500));

        ESP_LOGW(TAG, "[SELFTEST] --- ciclo completo; repetindo ---");
    }
}
#endif // MOTOR_SELFTEST

} // namespace

extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "=== Navegacao FLOOD-FILL + ENCODERS ===");

#if MOTOR_SELFTEST
    motorSelfTest(); // bancada: nunca retorna; nao entra na navegacao
#endif

    // 1) Bateria (sobe o barramento I2C compartilhado).
    if (!g_battery.init()) {
        ESP_LOGE(TAG, "Falha ao inicializar bateria/I2C.");
        return;
    }

    {
        const bool ina_ok = i2c_manager_probe(I2C_ADDR_INA226_BOARD);
        ESP_LOGI(TAG, "INA226 PROBE 0x%02X: %s | V=%.2f V | I=%.3f A | P=%.2f W | SOC=%.0f%%",
                 I2C_ADDR_INA226_BOARD, ina_ok ? "OK" : "FALHA",
                 g_battery.getVoltage(), g_battery.getCurrent(),
                 g_battery.getPower(), g_battery.getSOC());
        if (!ina_ok) {
            ESP_LOGW(TAG, "INA226 nao respondeu no 0x%02X: confira solda, SDA/SCL, "
                          "alimentacao e o strap de endereco (A0/A1).",
                     I2C_ADDR_INA226_BOARD);
        }
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

    // 4) Encoders (PCNT).
    if (!initEncoders()) {
        ESP_LOGE(TAG, "Encoders faltando; abortando.");
        return;
    }
    ESP_LOGI(TAG, "Encoders OK (PCNT quadratura x4).");

    // 5) Motores.
    initMotores();
    motores_para();

    // 5.1) Wi-Fi NAO-bloqueante: dispara a conexao e segue o boot. Cada envio
    //      consulta wifi_is_connected() na hora; a navegacao roda igual sem AP
    //      (a reconexao continua em background se a rede cair/voltar).
    inicializar_nvs();
    ESP_LOGI(TAG, "Conectando ao Wi-Fi '%s' em background...", WIFI_SSID);
    wifi_init_sta(WIFI_SSID, WIFI_PASS, /*timeout_ms=*/0);

    // 5.2) Botao de controle. So o D19 e usado: o D23 (tamanho) esta com defeito
    //      de hardware, entao o D19 faz tudo (toque curto = tamanho, segurar =
    //      largada; ver passo 6). botao_size fica declarado como reserva para o
    //      caso do hardware do D23 ser consertado depois.
    Botao botao_start(BUTTON_START_PIN);
    Botao botao_size(BUTTON_SIZE_PIN);
    botao_start.init();
    botao_size.init();
    (void)botao_size; // atualmente sem uso (D23 defeituoso)

    // 6) Selecao de LADO + TAMANHO da largada com UM UNICO botao (D19), porque o
    //    botao de tamanho (D23) esta com defeito de hardware. O D19 acumula as
    //    funcoes pelo tipo de acionamento:
    //       TOQUE CURTO    -> cicla as combinacoes lado+tamanho (kOpcoesLargada).
    //       SEGURAR >=0.7s -> confirma a combinacao e LARGA o mapeamento.
    int idx_lab = 0;
    {
        const OpcaoLargada &o = kOpcoesLargada[idx_lab];
        const int t = (int)o.tamanho;
        ESP_LOGI(TAG, "Aguardando largada: TOQUE CURTO cicla lado+tamanho "
                      "[%dx%d, largada %s]; SEGURE (>=0.7s) para mapear.",
                 t, t, nomeLado(o.lado));
    }
    while (true) {
        const Botao::Clique c = botao_start.clique();
        if (c == Botao::Clique::Curto) {
            idx_lab = (idx_lab + 1) % kNumOpcoesLargada;
            const OpcaoLargada &o = kOpcoesLargada[idx_lab];
            const int t = (int)o.tamanho;
            const Labirinto::Posicao ini = celulaLargada(o);
            ESP_LOGI(TAG, "Selecao: %dx%d, largada %s {%u,%u}", t, t,
                     nomeLado(o.lado), (unsigned)ini.x, (unsigned)ini.y);
        } else if (c == Botao::Clique::Longo) {
            const OpcaoLargada &o = kOpcoesLargada[idx_lab];
            const int t = (int)o.tamanho;
            ESP_LOGI(TAG, "Largada confirmada (%dx%d, %s): iniciando mapeamento.",
                     t, t, nomeLado(o.lado));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 6.1) Labirinto (flood-fill) com o lado/tamanho/goal escolhidos. A largada
    //      varia so na CELULA (lado oeste/leste); o heading e sempre Norte
    //      (a frente do robo na largada = Norte do modelo).
    const OpcaoLargada opc = kOpcoesLargada[idx_lab];
    const Labirinto::Posicao START = celulaLargada(opc);
    const Labirinto::Posicao GOAL  = opc.goal;
    g_dimensao = (int)opc.tamanho;
    g_lado_json = ladoJson(opc.lado);   // p/ o pacote de config (tipo 0)
    g_maze.configurar(opc.tamanho);
    Labirinto::InterfaceRobo robo;
    robo.virarPara = virarPara;
    robo.avancar   = avancar;
    g_maze.configurarRobo(robo);
    g_maze.iniciar(START, GOAL);
    g_heading = Labirinto::Direcao::Norte; // frente fisica na largada = Norte
    ESP_LOGI(TAG, "Labirinto %dx%d | largada %s {%u,%u} | objetivo {%u,%u}",
             (int)g_maze.tamanho(), (int)g_maze.tamanho(), nomeLado(opc.lado),
             (unsigned)START.x, (unsigned)START.y, (unsigned)GOAL.x, (unsigned)GOAL.y);

    // 7) Calibracao do gyro logo apos o botao (robo PARADO na largada).
    if (g_mpu_ok) { ESP_LOGI(TAG, "Calibrando gyro (mantenha o robo parado)..."); calibrar_bias_z(); }

    // 7.1) Marco zero da corrida + pacote 0 (Config Inicial) + task de heartbeat.
    //      Se o Wi-Fi ainda nao conectou, a tarefa_heartbeat envia a config
    //      atrasada assim que a conexao chegar.
    g_t0_ms = esp_timer_get_time() / 1000;
    atualizar_bateria();
    atualizar_temp();
    if (wifi_is_connected()) {
        g_config_enviada = true;
        enviar_configuracao_inicial(BACKEND_URL, telemetria_ts(),
                                    g_dimensao, g_lado_json, g_bateria_pct); // tipo 0
    }
    xTaskCreate(tarefa_heartbeat, "heartbeat", 4096, nullptr, 3, nullptr); // tipo 4

    // 8) MAPEAMENTO: sensoria paredes -> passo (que gira e avanca) ate alcancar
    //    o centro. O robo PARA no centro (nao volta sozinho a largada).
    ESP_LOGI(TAG, "=== MAPEAMENTO (ate o centro) ===");
    bool mapeou = false;
    bool abortou = false;
    while (true) {
        if (abortarPorTemperatura()) { abortou = true; break; }

        const Labirinto::LeituraSensores s = lerParedes();
        const Labirinto::Resultado r = g_maze.passo(s);
        const Labirinto::Posicao p = g_maze.posicao();
        ESP_LOGI(TAG, "passo -> %s | pos {%u,%u} | fase %d",
                 nome_resultado(r), (unsigned)p.x, (unsigned)p.y, (int)g_maze.fase());

        // Pacote 1: entrou em nova celula e sensoriou paredes (descoberta).
        if (g_maze.sensoriou()) {
            const Labirinto::Posicao ps = g_maze.posicaoSensoriada();
            const uint8_t w = g_maze.paredes(ps); // bits N=1,S=2,L=4,O=8 == campo 'w'
            // Debug: o conteudo exato do pacote 1 com as paredes decodificadas
            // (subir para LOGI/LOGW so quando precisar conferir com o web).
            ESP_LOGD(TAG, "TELE p1 -> x=%u y=%u w=%u [N=%d S=%d L=%d O=%d]",
                     (unsigned)ps.x, (unsigned)ps.y, (unsigned)w,
                     (w & Labirinto::ParedeNorte) ? 1 : 0,
                     (w & Labirinto::ParedeSul)   ? 1 : 0,
                     (w & Labirinto::ParedeLeste) ? 1 : 0,
                     (w & Labirinto::ParedeOeste) ? 1 : 0);
            if (wifi_is_connected()) {
                enviar_movimentacao(BACKEND_URL, telemetria_ts(), ps.x, ps.y, w);
            }
        }

        if (r == Labirinto::Resultado::AlcancouObjetivo) {
            motores_para();
            mapeou = true;
            ESP_LOGI(TAG, "=== Centro alcancado: mapeamento concluido. Robo parado no centro. ===");
            break;
        }
        if (r == Labirinto::Resultado::Bloqueado) {
            ESP_LOGE(TAG, "Bloqueado: sem movimento possivel. Parando.");
            // Pacote 3: fim sem sucesso.
            if (wifi_is_connected()) {
                enviar_fim_corrida(BACKEND_URL, telemetria_ts(), false, 0.0f, g_bateria_pct);
            }
            break;
        }
    }

    // 9) FAST RUN manual: reposicione o robo na LARGADA apontando para o NORTE
    //    e aperte o botao 1. O mapa aprendido e mantido (prepararFastRun).
    if (mapeou && !abortou) {
        ESP_LOGI(TAG, "Reposicione o robo na largada %s {%u,%u} apontando para o "
                      "NORTE (a mesma frente da largada) e aperte o botao (D19) "
                      "para a corrida rapida.",
                 nomeLado(opc.lado), (unsigned)START.x, (unsigned)START.y);
        while (!botao_start.clicado()) vTaskDelay(pdMS_TO_TICKS(10));

        g_maze.prepararFastRun();               // pos=inicio, heading=Norte, mapa intacto
        g_heading = Labirinto::Direcao::Norte;  // robo fisicamente reposicionado p/ NORTE
        g_fastrun_tiles = 0;
        g_fastrun_t0_ms = esp_timer_get_time() / 1000;

        // Pacote 2: rota otima conhecida no inicio da corrida rapida.
        if (wifi_is_connected()) {
            static Labirinto::Posicao rota_buf[Labirinto::kMaxCaminho];
            const uint16_t nrota = g_maze.rotaOtima(rota_buf, Labirinto::kMaxCaminho);
            enviar_rota_otimizada(BACKEND_URL, telemetria_ts(), rota_buf, nrota);
        }

        ESP_LOGI(TAG, "=== FAST RUN (PWM %d) ===", PWM_FAST);
        while (true) {
            if (abortarPorTemperatura()) break;

            // Otimizacao da reta: se o caminho otimo segue RETO por mais de 1
            // tile e o trecho termina numa PAREDE (curva forcada), dirige o
            // trecho inteiro SEM PARAR entre os tiles e para pela PAREDE FRONTAL
            // (referencia absoluta que corrige a deriva do encoder no trecho
            // longo), com desaceleracao na aproximacao. Depois recentraliza e
            // atualiza o modelo do maze, voltando ao ciclo normal que faz a curva.
            {
                Labirinto::Direcao dir_saida = Labirinto::Direcao::Nenhuma;
                bool termina_parede = false;
                const uint8_t n = g_maze.retaFastRun(dir_saida, termina_parede);
                if (n >= 2 && termina_parede) {
                    ESP_LOGI(TAG, "fastrun: reta de %u tiles -> dash ate a parede frontal",
                             (unsigned)n);
                    virarPara(dir_saida);                 // alinha uma unica vez
                    const Avanco ra = andarUmTile((int)n, FRONT_DASH_STOP_MM);
                    if (ra == Avanco::PAREDE) recentralizarFrontal();
                    g_maze.avancarModeloFastRun(dir_saida, n); // pos_/heading_ += n
                    g_fastrun_tiles += n;
                    vTaskDelay(pdMS_TO_TICKS(120));
                    continue;                             // reavalia do novo tile
                }
            }

            const Labirinto::LeituraSensores s = lerParedes();
            const Labirinto::Resultado r = g_maze.passo(s);
            const Labirinto::Posicao p = g_maze.posicao();
            ESP_LOGI(TAG, "fastrun -> %s | pos {%u,%u}",
                     nome_resultado(r), (unsigned)p.x, (unsigned)p.y);

            if (r == Labirinto::Resultado::FastRunCompleto) {
                ESP_LOGI(TAG, "=== LABIRINTO CONCLUIDO ===");
                // Pacote 3: fim com sucesso + velocidade media da fast run.
                float v_med = 0.0f;
                const int64_t dt_ms = (esp_timer_get_time() / 1000) - g_fastrun_t0_ms;
                if (dt_ms > 0) {
                    v_med = (g_fastrun_tiles * TILE_M) / (dt_ms / 1000.0f);
                }
                if (wifi_is_connected()) {
                    enviar_fim_corrida(BACKEND_URL, telemetria_ts(), true, v_med, g_bateria_pct);
                }
                break;
            }
            if (r == Labirinto::Resultado::Bloqueado) {
                ESP_LOGE(TAG, "Bloqueado na fast run. Parando.");
                // Pacote 3: fim sem sucesso.
                if (wifi_is_connected()) {
                    enviar_fim_corrida(BACKEND_URL, telemetria_ts(), false, 0.0f, g_bateria_pct);
                }
                break;
            }
        }
    }

    motores_para();
    while (true) vTaskDelay(pdMS_TO_TICKS(1000)); // idle
}
