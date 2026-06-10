/*Teste combinado: Motores + Encoders + Sensores (IMU, ToF)*/

#include <cstdio>
#include <math.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "pins.hpp"
#include "vl53l0x/IV_Vl53l0x.hpp"
#include "i2c_manager.hpp"

// --- Definições Sensores ---
#define MPU9250_ADDR      0x68
#define AK8963_ADDR       0x0C
#define INA226_ADDR       0x44

#define MPU_REG_WHO_AM_I      0x75
#define MPU_REG_PWR_MGMT_1    0x6B
#define MPU_REG_SMPLRT_DIV    0x19
#define MPU_REG_CONFIG        0x1A
#define MPU_REG_GYRO_CONFIG   0x1B
#define MPU_REG_ACCEL_CONFIG  0x1C
#define MPU_REG_ACCEL_CONFIG2 0x1D
#define MPU_REG_INT_PIN_CFG   0x37
#define MPU_REG_ACCEL_XOUT_H  0x3B

#define AK_REG_WHO_AM_I 0x00
#define AK_REG_ST1      0x02
#define AK_REG_HXL      0x03
#define AK_REG_CNTL1    0x0A

#define INA226_REG_CONFIG        0x00
#define INA226_REG_SHUNT_VOLTAGE 0x01
#define INA226_REG_BUS_VOLTAGE   0x02
#define INA226_REG_POWER         0x03
#define INA226_REG_CURRENT       0x04
#define INA226_REG_CALIBRATION   0x05
#define INA226_REG_MASK_ENABLE   0x06
#define INA226_REG_ALERT_LIMIT   0x07
#define INA226_REG_DIE_ID        0xFF

static const char *TAG = "COMBINED_TEST";

static SemaphoreHandle_t g_i2c_mutex = nullptr;
static i2c_master_dev_handle_t mpu_handle = nullptr;
static i2c_master_dev_handle_t mag_handle = nullptr;
static i2c_master_dev_handle_t ina226_handle = nullptr;

// Ordem de boot em cascata: FRONT_LEFT → FRONT → FRONT_RIGHT → LEFT → RIGHT
static IV_Vl53l0x g_tof_f_left({
    .position         = IV_Vl53l0x::Position::FRONT_LEFT,
    .address          = I2C_ADDR_VL53L0X_ALT_1,   // 0x2B
    .xshut_pin        = TOF_FRONT_LEFT_XSHUT_PIN,
    .i2c_speed_hz     = I2C_MANAGER_DEFAULT_SPEED_HZ,
    .timing_budget_ms = 20,
    .log_level        = espp::Logger::Verbosity::INFO,
});
static IV_Vl53l0x g_tof_front({
    .position         = IV_Vl53l0x::Position::FRONT,
    .address          = I2C_ADDR_VL53L0X_ALT_0,   // 0x2A
    .xshut_pin        = TOF_FRONT_XSHUT_PIN,
    .i2c_speed_hz     = I2C_MANAGER_DEFAULT_SPEED_HZ,
    .timing_budget_ms = 20,
    .log_level        = espp::Logger::Verbosity::INFO,
});
static IV_Vl53l0x g_tof_f_right({
    .position         = IV_Vl53l0x::Position::FRONT_RIGHT,
    .address          = I2C_ADDR_VL53L0X_ALT_2,   // 0x2C
    .xshut_pin        = TOF_FRONT_RIGHT_XSHUT_PIN,
    .i2c_speed_hz     = I2C_MANAGER_DEFAULT_SPEED_HZ,
    .timing_budget_ms = 20,
    .log_level        = espp::Logger::Verbosity::INFO,
});
static IV_Vl53l0x g_tof_left({
    .position         = IV_Vl53l0x::Position::LEFT,
    .address          = I2C_ADDR_VL53L0X_ALT_3,   // 0x2D
    .xshut_pin        = TOF_LEFT_XSHUT_PIN,
    .i2c_speed_hz     = I2C_MANAGER_DEFAULT_SPEED_HZ,
    .timing_budget_ms = 20,
    .log_level        = espp::Logger::Verbosity::INFO,
});
static IV_Vl53l0x g_tof_right({
    .position         = IV_Vl53l0x::Position::RIGHT,
    .address          = I2C_ADDR_VL53L0X_ALT_4,   // 0x2E
    .xshut_pin        = TOF_RIGHT_XSHUT_PIN,
    .i2c_speed_hz     = I2C_MANAGER_DEFAULT_SPEED_HZ,
    .timing_budget_ms = 20,
    .log_level        = espp::Logger::Verbosity::INFO,
});

// Definições Motores
#define PWM_MAX     1023
static const char* s_motor_state = "Parado";

static pcnt_unit_handle_t s_pcnt_unit_r = NULL;
static volatile int32_t   s_enc_total_r = 0;
static volatile int16_t   s_enc_last_r  = 0;
static pcnt_unit_handle_t s_pcnt_unit_l = NULL;
static volatile int32_t   s_enc_total_l = 0;
static volatile int16_t   s_enc_last_l  = 0;

// Funcoes Auxiliares i2c

static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value) {
    if(!dev) return ESP_FAIL;
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(dev, data, sizeof(data), pdMS_TO_TICKS(100));
}
static esp_err_t read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len) {
    if(!dev) return ESP_FAIL;
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, pdMS_TO_TICKS(100));
}
static esp_err_t read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value) {
    return read_regs(dev, reg, value, 1);
}
static int16_t be_i16(const uint8_t *data) { return (int16_t)((data[0] << 8) | data[1]); }
static int16_t le_i16(const uint8_t *data) { return (int16_t)((data[1] << 8) | data[0]); }

// Inicializacao dos Sensores

static esp_err_t init_mpu9250() {
    if (!i2c_manager_register_device(MPU9250_ADDR, I2C_MANAGER_DEFAULT_SPEED_HZ, &mpu_handle)) {
        ESP_LOGE(TAG, "Falha ao adicionar MPU9250 no i2c_manager");
        return ESP_FAIL;
    }
    uint8_t who = 0;
    if (read_reg(mpu_handle, MPU_REG_WHO_AM_I, &who) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao encontrar MPU9250 no endereço 0x%02X", MPU9250_ADDR);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "MPU9250 encontrado! WHO_AM_I = 0x%02X", who);
    write_reg(mpu_handle, MPU_REG_PWR_MGMT_1, 0x80);
    vTaskDelay(pdMS_TO_TICKS(100));
    write_reg(mpu_handle, MPU_REG_PWR_MGMT_1, 0x01);
    vTaskDelay(pdMS_TO_TICKS(10));
    write_reg(mpu_handle, MPU_REG_CONFIG,        0x03);
    write_reg(mpu_handle, MPU_REG_SMPLRT_DIV,    0x04);
    write_reg(mpu_handle, MPU_REG_GYRO_CONFIG,   0x00);
    write_reg(mpu_handle, MPU_REG_ACCEL_CONFIG,  0x00);
    write_reg(mpu_handle, MPU_REG_ACCEL_CONFIG2, 0x03);
    write_reg(mpu_handle, MPU_REG_INT_PIN_CFG,   0x02);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}
static void init_ak8963() {
    if (!i2c_manager_register_device(AK8963_ADDR, I2C_MANAGER_DEFAULT_SPEED_HZ, &mag_handle)) {
        return;
    }
    uint8_t who = 0;
    if (read_reg(mag_handle, AK_REG_WHO_AM_I, &who) != ESP_OK) {
        ESP_LOGW(TAG, "AK8963 não respondeu (Bússola indisponível).");
        return;
    }
    write_reg(mag_handle, AK_REG_CNTL1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
    write_reg(mag_handle, AK_REG_CNTL1, 0x16);
    vTaskDelay(pdMS_TO_TICKS(10));
}
static esp_err_t init_ina226() {
    if (!i2c_manager_register_device(INA226_ADDR, I2C_MANAGER_DEFAULT_SPEED_HZ, &ina226_handle)) {
        ESP_LOGE(TAG, "Falha ao adicionar INA226 no i2c_manager");
        return ESP_FAIL;
    }
    uint8_t id[2] = {0};
    if (read_regs(ina226_handle, INA226_REG_DIE_ID, id, 2) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao encontrar INA226 no endereço 0x%02X", INA226_ADDR);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "INA226 encontrado! DIE_ID = 0x%04X", (id[0] << 8) | id[1]);

    // Configuração: Average=16, VoltConvTime=1.1ms, CurrentConvTime=1.1ms, Mode=Continuous
    // Config = 0x4527: bit15=1 (reset), bits14-11=0111 (16 amostras), bits10-8=010 (1.1ms), bits7-5=010 (1.1ms), bits2-0=111 (continuous)
    write_reg(ina226_handle, INA226_REG_CONFIG, 0x45);
    write_reg(ina226_handle, INA226_REG_CONFIG + 1, 0x27);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Calibração: R_shunt = 0.1Ω, Max Current = 3A => Calibration = 512 (0x0200)
    write_reg(ina226_handle, INA226_REG_CALIBRATION, 0x02);
    write_reg(ina226_handle, INA226_REG_CALIBRATION + 1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}

// Funcoes Motores e Encoders

static void encoder_read(void) {
    int raw_r = 0;
    pcnt_unit_get_count(s_pcnt_unit_r, &raw_r);
    int16_t cur_r = (int16_t)raw_r;
    s_enc_total_r += (cur_r - s_enc_last_r);
    s_enc_last_r = cur_r;
    int raw_l = 0;
    pcnt_unit_get_count(s_pcnt_unit_l, &raw_l);
    int16_t cur_l = (int16_t)raw_l;
    s_enc_total_l += (cur_l - s_enc_last_l);
    s_enc_last_l = cur_l;
}
static void encoder_reset(void) {
    pcnt_unit_clear_count(s_pcnt_unit_r);
    s_enc_total_r = 0;
    s_enc_last_r  = 0;
    pcnt_unit_clear_count(s_pcnt_unit_l);
    s_enc_total_l = 0;
    s_enc_last_l  = 0;
}
static void pwm_set_ambos(uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}
static void motores_frente(uint32_t duty) {
    s_motor_state = "Frente";
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 1);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 0);
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 1);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 0);
    pwm_set_ambos(duty);
}
static void motores_re(uint32_t duty) {
    s_motor_state = "Trás";
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 1);
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 0);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 1);
    pwm_set_ambos(duty);
}
static void motores_para(void) {
    s_motor_state = "Parado";
    pwm_set_ambos(0);
    gpio_set_level(MOTOR_RIGHT_IN1_PIN, 0);
    gpio_set_level(MOTOR_RIGHT_IN2_PIN, 0);
    gpio_set_level(MOTOR_LEFT_IN1_PIN, 0);
    gpio_set_level(MOTOR_LEFT_IN2_PIN, 0);
}

// Tasks do Freertos

// Task única que lê todos os 5 ToFs de forma sequencial sob o mesmo Mutex.
static void tofs_reading_task(void *) {
    const TickType_t period = pdMS_TO_TICKS(1000); // Imprime a cada 1 segundo 
    TickType_t last_wake    = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&last_wake, period);
        if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(150)) != pdTRUE) {
            ESP_LOGW(TAG, "Task ToFs perdeu a vez no I2C");
            continue;
        }
        float d_fl = g_tof_f_left.readDistanceMm();
        float d_f  = g_tof_front.readDistanceMm();
        float d_fr = g_tof_f_right.readDistanceMm();
        float d_l  = g_tof_left.readDistanceMm();
        float d_r  = g_tof_right.readDistanceMm();
        xSemaphoreGive(g_i2c_mutex);
        std::printf("TOF | L:%5.0f | FL:%5.0f | F:%5.0f | FR:%5.0f | R:%5.0f |\n",
                    d_l, d_fl, d_f, d_fr, d_r);
    }
}
static void mpu_task(void *) {
    const TickType_t period = pdMS_TO_TICKS(1000); // Imprime a cada 1 segundo (era 500ms)
    TickType_t last_wake    = xTaskGetTickCount();
    uint8_t data[14] = {0};
    while (true) {
        vTaskDelayUntil(&last_wake, period);
        if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(150)) != pdTRUE) {
            ESP_LOGW(TAG, "MPU perdeu a vez no I2C");
            continue;
        }
        if (mpu_handle && read_regs(mpu_handle, MPU_REG_ACCEL_XOUT_H, data, sizeof(data)) == ESP_OK) {
            ESP_LOGI(TAG, "MPU | accel[g] X=% .3f Y=% .3f Z=% .3f | gyro[dps] X=% .2f Y=% .2f Z=% .2f",
                     be_i16(&data[0])  / 16384.0f,
                     be_i16(&data[2])  / 16384.0f,
                     be_i16(&data[4])  / 16384.0f,
                     be_i16(&data[8])  / 131.0f,
                     be_i16(&data[10]) / 131.0f,
                     be_i16(&data[12]) / 131.0f);
        }
        if (mag_handle) {
            uint8_t st1 = 0;
            if (read_reg(mag_handle, AK_REG_ST1, &st1) == ESP_OK && (st1 & 0x01)) {
                uint8_t mag_data[7] = {0};
                if (read_regs(mag_handle, AK_REG_HXL, mag_data, sizeof(mag_data)) == ESP_OK && !(mag_data[6] & 0x08)) {
                    ESP_LOGI(TAG, "MAG | mag[uT] X=% .2f Y=% .2f Z=% .2f",
                             le_i16(&mag_data[0]) * 0.15f,
                             le_i16(&mag_data[2]) * 0.15f,
                             le_i16(&mag_data[4]) * 0.15f);
                }
            }
        }
        xSemaphoreGive(g_i2c_mutex);
    }
}
static void encoder_task(void *arg) {
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000)); // Imprime a cada 1 segundo
        encoder_read();
        ESP_LOGI(TAG, "Estado: %s | Esq (L): %ld | Dir (R): %ld",
                 s_motor_state, (long)s_enc_total_l, (long)s_enc_total_r);
    }
}
static void ina226_task(void *) {
    const TickType_t period = pdMS_TO_TICKS(1000);
    TickType_t last_wake = xTaskGetTickCount();
    uint8_t data[2] = {0};
    while (true) {
        vTaskDelayUntil(&last_wake, period);
        if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(150)) != pdTRUE) {
            ESP_LOGW(TAG, "INA226 perdeu a vez no I2C");
            continue;
        }
        if (ina226_handle) {
            // Ler Bus Voltage (V_bus em mV, LSB = 1.25mV)
            if (read_regs(ina226_handle, INA226_REG_BUS_VOLTAGE, data, 2) == ESP_OK) {
                int16_t bus_raw = be_i16(data); // INA226 usa todos os 16 bits
                float bus_v = bus_raw * 0.00125f;

                // Ler Current (Corrente em mA, depende da calibração)
                if (read_regs(ina226_handle, INA226_REG_CURRENT, data, 2) == ESP_OK) {
                    int16_t current_raw = be_i16(data);
                    float current_ma = current_raw * 0.1f;  // 0.1mA por LSB (com calibração 512)

                    ESP_LOGI(TAG, "INA226 | V_bus=%.2f V | I_total=%+.1f mA", bus_v, current_ma);
                }
            }
        }
        xSemaphoreGive(g_i2c_mutex);
    }
}

// Inicializar Motores e Encoders

static bool hw_init(void) {
    gpio_config_t cfg = {};
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pin_bit_mask = (1ULL << MOTOR_STBY_PIN) | 
                       (1ULL << MOTOR_RIGHT_IN1_PIN) | (1ULL << MOTOR_RIGHT_IN2_PIN) |
                       (1ULL << MOTOR_LEFT_IN1_PIN)  | (1ULL << MOTOR_LEFT_IN2_PIN);
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
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
    ledc_channel_config_t ch_r = {};
    ch_r.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_r.channel = LEDC_CHANNEL_1;
    ch_r.timer_sel = LEDC_TIMER_0;
    ch_r.gpio_num = MOTOR_RIGHT_PWM_PIN;
    ch_r.duty = 0;
    ledc_channel_config(&ch_r);
    ledc_channel_config_t ch_l = {};
    ch_l.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_l.channel = LEDC_CHANNEL_0;
    ch_l.timer_sel = LEDC_TIMER_0;
    ch_l.gpio_num = MOTOR_LEFT_PWM_PIN;
    ch_l.duty = 0;
    ledc_channel_config(&ch_l);
    gpio_config_t enc_cfg = {};
    enc_cfg.mode = GPIO_MODE_INPUT;
    enc_cfg.pin_bit_mask = (1ULL << MOTOR_RIGHT_ENC_A_PIN) | (1ULL << MOTOR_RIGHT_ENC_B_PIN) |
                           (1ULL << MOTOR_LEFT_ENC_A_PIN)  | (1ULL << MOTOR_LEFT_ENC_B_PIN);
    enc_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    enc_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&enc_cfg);
    pcnt_unit_config_t unit_cfg = {};
    unit_cfg.high_limit = 32767;
    unit_cfg.low_limit = -32768;
    pcnt_glitch_filter_config_t filter_cfg = { .max_glitch_ns = 12000 };
    pcnt_new_unit(&unit_cfg, &s_pcnt_unit_r);
    pcnt_unit_set_glitch_filter(s_pcnt_unit_r, &filter_cfg);
    pcnt_chan_config_t chan_a_r_cfg = {};
    chan_a_r_cfg.edge_gpio_num = MOTOR_RIGHT_ENC_A_PIN;
    chan_a_r_cfg.level_gpio_num = MOTOR_RIGHT_ENC_B_PIN;
    pcnt_channel_handle_t chan_a_r = NULL;
    pcnt_new_channel(s_pcnt_unit_r, &chan_a_r_cfg, &chan_a_r);
    pcnt_channel_set_edge_action(chan_a_r, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chan_a_r, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_chan_config_t chan_b_r_cfg = {};
    chan_b_r_cfg.edge_gpio_num = MOTOR_RIGHT_ENC_B_PIN;
    chan_b_r_cfg.level_gpio_num = MOTOR_RIGHT_ENC_A_PIN;
    pcnt_channel_handle_t chan_b_r = NULL;
    pcnt_new_channel(s_pcnt_unit_r, &chan_b_r_cfg, &chan_b_r);
    pcnt_channel_set_edge_action(chan_b_r, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(chan_b_r, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_unit_enable(s_pcnt_unit_r);
    pcnt_unit_clear_count(s_pcnt_unit_r);
    pcnt_unit_start(s_pcnt_unit_r);
    pcnt_new_unit(&unit_cfg, &s_pcnt_unit_l);
    pcnt_unit_set_glitch_filter(s_pcnt_unit_l, &filter_cfg);
    pcnt_chan_config_t chan_a_l_cfg = {};
    chan_a_l_cfg.edge_gpio_num = MOTOR_LEFT_ENC_A_PIN;
    chan_a_l_cfg.level_gpio_num = MOTOR_LEFT_ENC_B_PIN;
    pcnt_channel_handle_t chan_a_l = NULL;
    pcnt_new_channel(s_pcnt_unit_l, &chan_a_l_cfg, &chan_a_l);
    pcnt_channel_set_edge_action(chan_a_l, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chan_a_l, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_chan_config_t chan_b_l_cfg = {};
    chan_b_l_cfg.edge_gpio_num = MOTOR_LEFT_ENC_B_PIN;
    chan_b_l_cfg.level_gpio_num = MOTOR_LEFT_ENC_A_PIN;
    pcnt_channel_handle_t chan_b_l = NULL;
    pcnt_new_channel(s_pcnt_unit_l, &chan_b_l_cfg, &chan_b_l);
    pcnt_channel_set_edge_action(chan_b_l, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(chan_b_l, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_unit_enable(s_pcnt_unit_l);
    pcnt_unit_clear_count(s_pcnt_unit_l);
    pcnt_unit_start(s_pcnt_unit_l);
    gpio_set_level(MOTOR_STBY_PIN, 1); // Acorda a Ponte H
    return true;
}

// Funcao Principal

extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "=== Teste Combinado: IMU, ToF, Motores, Encoders ===");
    // 1. Cria o mutex que serializa o acesso ao barramento I2C
    g_i2c_mutex = xSemaphoreCreateMutex();
    if (g_i2c_mutex == nullptr) {
        ESP_LOGE(TAG, "Falha ao criar o Mutex do I2C");
        return;
    }
    // 2. Inicializa o barramento I2C
    if (!i2c_manager_init()) {
        ESP_LOGE(TAG, "Falha ao inicializar o barramento I2C");
        return;
    }
    // 3. Desliga TODOS os 5 ToFs
    g_tof_f_left.disable();
    g_tof_front.disable();
    g_tof_f_right.disable();
    g_tof_left.disable();
    g_tof_right.disable();
    vTaskDelay(pdMS_TO_TICKS(20));
    // 4. Boot em cascata dos ToFs
    if (!g_tof_f_left.init())  ESP_LOGE(TAG, "Falha no ToF FRONT_LEFT");
    else ESP_LOGI(TAG, "ToF FRONT_LEFT  em 0x%02X", g_tof_f_left.address());
    if (!g_tof_front.init())   ESP_LOGE(TAG, "Falha no ToF FRONT");
    else ESP_LOGI(TAG, "ToF FRONT       em 0x%02X", g_tof_front.address());
    if (!g_tof_f_right.init()) ESP_LOGE(TAG, "Falha no ToF FRONT_RIGHT");
    else ESP_LOGI(TAG, "ToF FRONT_RIGHT em 0x%02X", g_tof_f_right.address());
    if (!g_tof_left.init())    ESP_LOGE(TAG, "Falha no ToF LEFT");
    else ESP_LOGI(TAG, "ToF LEFT        em 0x%02X", g_tof_left.address());
    if (!g_tof_right.init())   ESP_LOGE(TAG, "Falha no ToF RIGHT");
    else ESP_LOGI(TAG, "ToF RIGHT       em 0x%02X", g_tof_right.address());
    // 5. Inicializa o IMU e a Bússola
    if (init_mpu9250() == ESP_OK) {
        init_ak8963();
        ESP_LOGI(TAG, "Sensor IMU inicializado no barramento compartilhado.");
    } else {
        ESP_LOGE(TAG, "Sensor IMU não respondeu.");
    }
    // 6. Inicializa o sensor de corrente INA226
    if (init_ina226() == ESP_OK) {
        ESP_LOGI(TAG, "Sensor INA226 inicializado no endereço 0x%02X", INA226_ADDR);
    } else {
        ESP_LOGE(TAG, "Sensor INA226 não respondeu.");
    }
    // 7. Inicializa hardware dos motores/encoders
    if (!hw_init()) {
        ESP_LOGE(TAG, "Erro fatal: falha no hardware dos motores/encoders.");
        return;
    }
    ESP_LOGI(TAG, "Iniciando Tasks...");
    xTaskCreatePinnedToCore(tofs_reading_task, "tofs_task", 4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(mpu_task,          "mpu_task",  4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(ina226_task,       "ina226_task", 4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(encoder_task,      "enc_task",  4096, nullptr, 5, nullptr, 1);
    // Loop contínuo alternando as direções do motor
    while (true) { 
        encoder_reset();
        motores_frente(PWM_MAX / 2);
        vTaskDelay(pdMS_TO_TICKS(3000));
        motores_para();
        vTaskDelay(pdMS_TO_TICKS(1000));
        encoder_reset();
        motores_re(PWM_MAX / 2);
        vTaskDelay(pdMS_TO_TICKS(3000));
        motores_para();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}