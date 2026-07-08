#include "battery/battery.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <system_error>
#include <cmath>
#include <cstring>

#include "i2c_manager.hpp"
#define I2C_TIMEOUT_MS      50


#define BATTERY_CAPACITY_AH     2.0f
#define BATTERY_CAPACITY_AS     (BATTERY_CAPACITY_AH * 3600.0f)
#define BATTERY_VOLTAGE_MAX     8.4f
#define BATTERY_VOLTAGE_MIN     6.6f
#define BATTERY_REST_CURRENT_A  0.05f
#define VOLTAGE_CORRECTION_ALPHA 0.01f

// MOCK da bateria: em 1, ignora o INA226 e usa valores fixos (apenas para
// bancada com o hardware defeituoso). Em 0 (padrao), le o INA226 real; o
// init() loga tensao/corrente iniciais para validar a leitura.
#define BATTERY_MOCK 0
#define BATTERY_MOCK_VOLTAGE_V  7.8f
#define BATTERY_MOCK_CURRENT_A  0.30f
#define BATTERY_MOCK_POWER_W    (BATTERY_MOCK_VOLTAGE_V * BATTERY_MOCK_CURRENT_A)
#define BATTERY_MOCK_SOC        85.0f


static bool i2c_write_cb(uint8_t /*dev_addr*/, const uint8_t *data, size_t len) {
    return i2c_manager_write(I2C_ADDR_INA226_BOARD, data, len, I2C_TIMEOUT_MS);
}

static bool i2c_read_register_cb(uint8_t /*dev_addr*/, uint8_t reg_addr,
                                  uint8_t *data, size_t len) {
    return i2c_manager_read_register(I2C_ADDR_INA226_BOARD, reg_addr,
                                     data, len, I2C_TIMEOUT_MS);
}


Battery::Battery()
        : ina_sensor(nullptr),
        soc_(50.0f),
        last_update_us_(0),
        capacity_as_(BATTERY_CAPACITY_AS)
{
    memset(filters_, 0, sizeof(filters_));
}

bool Battery::init() {
    // Inicializa o barramento I2C compartilhado via manager. Isso e necessario
    // mesmo no modo MOCK porque os ToFs e o IMU dependem do mesmo barramento.
    if (!i2c_manager_init()) {
        ESP_LOGE("Battery", "Failed to init shared I2C bus");
        return false;
    }

#if BATTERY_MOCK
    // MOCK: nao registra/configura o INA226 (hardware com defeito). Apenas
    // semeia o SOC com um valor fixo e segue em frente.
    ESP_LOGW("Battery", "MOCK ativo: INA226 ignorado, usando valores fixos");
    ina_sensor      = nullptr;
    soc_            = BATTERY_MOCK_SOC;
    last_update_us_ = esp_timer_get_time();
    return true;
#else
    if (!i2c_manager_register_device(I2C_ADDR_INA226_BOARD)) {
        ESP_LOGE("Battery", "Failed to register INA226 on I2C bus");
        return false;
    }

    // Configura o INA226 com callbacks de I2C e parametros do shunt.
    espp::Ina226::Config config{};
    config.device_address        = I2C_ADDR_INA226_BOARD;
    config.write                 = i2c_write_cb;
    config.read_register         = i2c_read_register_cb;
    config.current_lsb           = 0.0001f;
    config.shunt_resistance_ohms = 0.0333333f; // Físico: 3 resistores de 0.1 ohm em paralelo
    ina_sensor = new espp::Ina226(config);

    // Diagnostico do INA226. Corrente e potencia so sao calculadas pelo chip se o
    // registrador CALIBRATION estiver programado; a tensao (bus) e independente
    // disso. Se o auto-init do driver abortou no check de ID (comum em modulos
    // clones), a calibracao nunca foi escrita e corrente/potencia ficam em 0
    // enquanto a tensao continua valida. Logamos IDs + shunt bruto e forcamos a
    // calibracao para descartar esse caso.
    {
        std::error_code ec;
        const uint16_t man_id  = ina_sensor->manufacturer_id(ec);
        const uint16_t die_id  = ina_sensor->die_id(ec);
        const float    v_shunt = ina_sensor->shunt_voltage_volts(ec);
        ESP_LOGI("Battery",
                 "INA226 diag: manId=0x%04X (esperado 0x5449) | dieId=0x%04X "
                 "(esperado 0x2260) | Vshunt=%.6f V",
                 man_id, die_id, v_shunt);
        if (man_id != 0x5449 || die_id != 0x2260) {
            ESP_LOGW("Battery",
                     "IDs do INA226 nao batem: possivel modulo clone. Forcando "
                     "calibracao para habilitar leitura de corrente/potencia.");
        }
        // Forca a calibracao independente do check de ID do driver. Se Vshunt for
        // ~0 aqui mesmo com carga, a corrente=0 e problema de hardware (shunt/
        // conexoes IN+/IN-), nao de calibracao.
        ina_sensor->calibrate(config.current_lsb, config.shunt_resistance_ohms, ec);
        if (ec) {
            ESP_LOGW("Battery", "Falha ao escrever calibracao do INA226: %s",
                     ec.message().c_str());
        }
    }

    float v0 = getVoltage();
    for (int tent = 0; v0 < 1.0f && tent < 20; ++tent) {
        vTaskDelay(pdMS_TO_TICKS(10));
        v0 = getVoltage();
    }
    const float i0 = getCurrent();
    const float p0 = getPower();
    const float seeds[FILTER_COUNT] = {i0, v0, p0};

    for (int f = 0; f < FILTER_COUNT; ++f) {
        for (int i = 0; i < FILTER_SIZE; ++i)
            filters_[f].buf[i] = seeds[f];
        filters_[f].count = FILTER_SIZE;
        filters_[f].lpf   = seeds[f];
    }

    soc_            = voltageToSOC(v0);
    last_update_us_ = esp_timer_get_time();

    // Log de validacao da leitura real do INA226. Uma tensao ~0 V indica que o
    // sensor nao respondeu (endereco errado, shunt, solda): conferir a PCB.
    ESP_LOGI("Battery", "INA226 OK: V=%.2f V | I=%.3f A | P=%.2f W | SOC=%.0f%%",
             v0, i0, p0, soc_);
    if (v0 < 1.0f) {
        ESP_LOGW("Battery", "Tensao lida (%.2f V) parece invalida; verifique o "
                            "INA226 (endereco 0x%02X, shunt, conexoes)",
                 v0, I2C_ADDR_INA226_BOARD);
    }
    return true;
#endif // BATTERY_MOCK
}

void Battery::update() {
#if BATTERY_MOCK
    // MOCK: mantem o SOC fixo (sem integracao de corrente real).
    soc_ = BATTERY_MOCK_SOC;
    return;
#else
    const int64_t now_us = esp_timer_get_time();
    const float delta_s  = static_cast<float>(now_us - last_update_us_) / 1e6f;
    last_update_us_ = now_us;

    const float current = getCurrent();
    const float voltage = getVoltage();

    soc_ += (current * delta_s / capacity_as_) * 100.0f;

    // Filtro complementar: corrige SOC pela tensao quando a bateria esta em repouso.
    if (fabsf(current) < BATTERY_REST_CURRENT_A)
        soc_ += VOLTAGE_CORRECTION_ALPHA * (voltageToSOC(voltage) - soc_);

    if (soc_ > 100.0f) soc_ = 100.0f;
    if (soc_ <   0.0f) soc_ =   0.0f;
#endif // BATTERY_MOCK
}

float Battery::getVoltage() {
#if BATTERY_MOCK
    return BATTERY_MOCK_VOLTAGE_V;
#else
    if (!ina_sensor) return 0.0f;
    std::error_code ec;
    return ina_sensor->bus_voltage_volts(ec);
#endif
}

float Battery::getCurrent() {
#if BATTERY_MOCK
    return BATTERY_MOCK_CURRENT_A;
#else
    if (!ina_sensor) return 0.0f;
    std::error_code ec;
    return ina_sensor->current_amps(ec);
#endif
}

float Battery::getPower() {
#if BATTERY_MOCK
    return BATTERY_MOCK_POWER_W;
#else
    if (!ina_sensor) return 0.0f;
    std::error_code ec;
    return ina_sensor->power_watts(ec);
#endif
}

float Battery::getSOC() {
    return soc_;
}

float Battery::applyFilter(Filter f, float raw) {
    FilterState &s = filters_[f];

    s.buf[s.idx] = raw;
    s.idx = (s.idx + 1) % FILTER_SIZE;
    if (s.count < FILTER_SIZE) ++s.count;

    float sum = 0.0f;
    for (uint8_t i = 0; i < s.count; ++i) sum += s.buf[i];

    s.lpf = FILTER_ALPHA * (sum / s.count) + (1.0f - FILTER_ALPHA) * s.lpf;
    return s.lpf;
}

float Battery::getCurrentFiltered() { return applyFilter(CURRENT, getCurrent()); }
float Battery::getVoltageFiltered() { return applyFilter(VOLTAGE, getVoltage()); }
float Battery::getPowerFiltered()   { return applyFilter(POWER,   getPower());   }

float Battery::voltageToSOC(float voltage) {
    if (voltage <= BATTERY_VOLTAGE_MIN) return 0.0f;
    if (voltage >= BATTERY_VOLTAGE_MAX) return 100.0f;
    return ((voltage - BATTERY_VOLTAGE_MIN) /
            (BATTERY_VOLTAGE_MAX - BATTERY_VOLTAGE_MIN)) * 100.0f;
}