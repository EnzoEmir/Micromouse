// Driver VL53L0X baseado na biblioteca Pololu (https://github.com/pololu/vl53l0x-arduino)
// Adaptado para ESP-IDF com o i2c_manager do projeto.

#include "vl53l0x/IV_Vl53l0x.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

// Endereços de registrador do VL53L0X (8-bit)
namespace Reg {
    static constexpr uint8_t SYSRANGE_START                              = 0x00;
    static constexpr uint8_t SYSTEM_SEQUENCE_CONFIG                      = 0x01;
    static constexpr uint8_t SYSTEM_INTERMEASUREMENT_PERIOD              = 0x04;
    static constexpr uint8_t SYSTEM_INTERRUPT_CONFIG_GPIO                = 0x0A;
    static constexpr uint8_t SYSTEM_INTERRUPT_CLEAR                      = 0x0B;
    static constexpr uint8_t RESULT_INTERRUPT_STATUS                     = 0x13;
    static constexpr uint8_t RESULT_RANGE_STATUS                         = 0x14;
    static constexpr uint8_t MSRC_CONFIG_CONTROL                         = 0x60;
    static constexpr uint8_t MSRC_CONFIG_TIMEOUT_MACROP                  = 0x46;
    static constexpr uint8_t PRE_RANGE_CONFIG_VCSEL_PERIOD               = 0x50;
    static constexpr uint8_t PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI          = 0x51;
    static constexpr uint8_t PRE_RANGE_CONFIG_TIMEOUT_MACROP_LO          = 0x52;
    static constexpr uint8_t FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT = 0x44;
    static constexpr uint8_t FINAL_RANGE_CONFIG_VCSEL_PERIOD             = 0x70;
    static constexpr uint8_t FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI        = 0x71;
    static constexpr uint8_t FINAL_RANGE_CONFIG_TIMEOUT_MACROP_LO        = 0x72;
    static constexpr uint8_t GPIO_HV_MUX_ACTIVE_HIGH                     = 0x84;
    static constexpr uint8_t VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV            = 0x89;
    static constexpr uint8_t GLOBAL_CONFIG_SPAD_ENABLES_REF_0            = 0xB0;
    static constexpr uint8_t GLOBAL_CONFIG_REF_EN_START_SELECT           = 0xB6;
    static constexpr uint8_t DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD         = 0x4E;
    static constexpr uint8_t DYNAMIC_SPAD_REF_EN_START_OFFSET            = 0x4F;
    static constexpr uint8_t POWER_MANAGEMENT_GO1_POWER_FORCE            = 0x80;
    static constexpr uint8_t IDENTIFICATION_MODEL_ID                     = 0xC0;
    static constexpr uint8_t OSC_CALIBRATE_VAL                           = 0xF8;
    static constexpr uint8_t ALGO_PHASECAL_CONFIG_TIMEOUT                = 0x30;
}

// Helpers de posição
static const char* position_str(IV_Vl53l0x::Position pos) {
    switch (pos) {
        case IV_Vl53l0x::Position::FRONT:        return "FRONT";
        case IV_Vl53l0x::Position::FRONT_LEFT:   return "FRONT_LEFT";
        case IV_Vl53l0x::Position::FRONT_RIGHT:  return "FRONT_RIGHT";
        case IV_Vl53l0x::Position::LEFT:         return "LEFT";
        case IV_Vl53l0x::Position::RIGHT:        return "RIGHT";
        case IV_Vl53l0x::Position::BACK:         return "BACK";
        default:                                 return "CUSTOM";
    }
}

// Constructor
IV_Vl53l0x::IV_Vl53l0x(const Config& config)
    : config_(config),
      log_tag_(std::string("VL53L0X[") + position_str(config.position) + "]"),
      logger_({.tag = log_tag_.c_str(), .level = config.log_level}) {}

std::string_view IV_Vl53l0x::positionName() const {
    return position_str(config_.position);
}

// XSHUT
void IV_Vl53l0x::configureXshut(bool high) {
    if (config_.xshut_pin == GPIO_NUM_NC) return;

    gpio_config_t io_cfg    = {};
    io_cfg.pin_bit_mask     = 1ULL << config_.xshut_pin;
    io_cfg.mode             = GPIO_MODE_OUTPUT;
    io_cfg.pull_up_en       = GPIO_PULLUP_DISABLE;
    io_cfg.pull_down_en     = GPIO_PULLDOWN_DISABLE;
    io_cfg.intr_type        = GPIO_INTR_DISABLE;
    gpio_config(&io_cfg);

    gpio_set_level(config_.xshut_pin, high ? 1 : 0);
    if (high) vTaskDelay(pdMS_TO_TICKS(20)); // Aguarda o sensor subir
    else vTaskDelay(pdMS_TO_TICKS(10)); // Aguarda o hardware descarregar
}

void IV_Vl53l0x::enable()  { configureXshut(true); }
void IV_Vl53l0x::disable() { configureXshut(false); }

// I2C helpers (protocolo 8-bit de endereço do VL53L0X)
bool IV_Vl53l0x::writeReg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_manager_write(config_.address, buf, 2);
}

bool IV_Vl53l0x::writeReg16(uint8_t reg, uint16_t val) {
    uint8_t buf[3] = {reg, static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val & 0xFF)};
    return i2c_manager_write(config_.address, buf, 3);
}

bool IV_Vl53l0x::writeMulti(uint8_t reg, const uint8_t* data, size_t len) {
    uint8_t buf[len + 1];
    buf[0] = reg;
    std::memcpy(buf + 1, data, len);
    return i2c_manager_write(config_.address, buf, len + 1);
}

uint8_t IV_Vl53l0x::readReg8(uint8_t reg) {
    uint8_t val = 0;
    i2c_manager_write_read(config_.address, &reg, 1, &val, 1, 10);
    return val;
}

uint16_t IV_Vl53l0x::readReg16(uint8_t reg) {
    uint8_t data[2] = {};
    i2c_manager_write_read(config_.address, &reg, 1, data, 2, 10);
    return static_cast<uint16_t>((data[0] << 8) | data[1]);
}

bool IV_Vl53l0x::readMulti(uint8_t reg, uint8_t* data, size_t len) {
    return i2c_manager_write_read(config_.address, &reg, 1, data, len, 10);
}

// Timeout helpers (Pololu)
uint16_t IV_Vl53l0x::decodeTimeout(uint16_t val) {
    return static_cast<uint16_t>((val & 0x00FF) << ((val & 0xFF00) >> 8)) + 1;
}

uint16_t IV_Vl53l0x::encodeTimeout(uint32_t mclks) {
    if (mclks == 0) return 0;
    uint32_t ls = mclks - 1;
    uint16_t ms = 0;
    while ((ls & 0xFFFFFF00) > 0) { ls >>= 1; ms++; }
    return static_cast<uint16_t>((ms << 8) | (ls & 0xFF));
}

uint32_t IV_Vl53l0x::mclksToUs(uint16_t mclks, uint8_t vcsel_pclks) {
    uint32_t macro_period_ns = (2304UL * vcsel_pclks * 1655UL + 500UL) / 1000UL;
    return (static_cast<uint32_t>(mclks) * macro_period_ns + 500UL) / 1000UL;
}

uint32_t IV_Vl53l0x::usToMclks(uint32_t us, uint8_t vcsel_pclks) {
    uint32_t macro_period_ns = (2304UL * vcsel_pclks * 1655UL + 500UL) / 1000UL;
    return ((us * 1000UL) + (macro_period_ns / 2)) / macro_period_ns;
}

// Sequence step helpers
void IV_Vl53l0x::getSequenceStepEnables(SequenceStepEnables* e) {
    uint8_t seq = readReg8(Reg::SYSTEM_SEQUENCE_CONFIG);
    e->tcc         = (seq >> 4) & 0x1;
    e->dss         = (seq >> 3) & 0x1;
    e->msrc        = (seq >> 2) & 0x1;
    e->pre_range   = (seq >> 6) & 0x1;
    e->final_range = (seq >> 7) & 0x1;
}

void IV_Vl53l0x::getSequenceStepTimeouts(const SequenceStepEnables* e, SequenceStepTimeouts* t) {
    t->pre_range_vcsel_period_pclks  = (readReg8(Reg::PRE_RANGE_CONFIG_VCSEL_PERIOD) + 1) << 1;
    t->final_range_vcsel_period_pclks = (readReg8(Reg::FINAL_RANGE_CONFIG_VCSEL_PERIOD) + 1) << 1;

    t->msrc_dss_tcc_mclks = readReg8(Reg::MSRC_CONFIG_TIMEOUT_MACROP) + 1;
    t->msrc_dss_tcc_us    = mclksToUs(t->msrc_dss_tcc_mclks, t->pre_range_vcsel_period_pclks);

    t->pre_range_mclks = decodeTimeout(readReg16(Reg::PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI));
    t->pre_range_us    = mclksToUs(t->pre_range_mclks, t->pre_range_vcsel_period_pclks);

    t->final_range_mclks = decodeTimeout(readReg16(Reg::FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI));
    if (e->pre_range) t->final_range_mclks -= t->pre_range_mclks;
    t->final_range_us = mclksToUs(t->final_range_mclks, t->final_range_vcsel_period_pclks);
}

bool IV_Vl53l0x::setMeasurementTimingBudget(uint32_t budget_us) {
    static constexpr uint32_t StartOverhead      = 1910;
    static constexpr uint32_t EndOverhead        = 960;
    static constexpr uint32_t MsrcOverhead       = 660;
    static constexpr uint32_t TccOverhead        = 590;
    static constexpr uint32_t DssOverhead        = 690;
    static constexpr uint32_t PreRangeOverhead   = 660;
    static constexpr uint32_t FinalRangeOverhead = 550;
    static constexpr uint32_t MinBudget          = 20000;

    if (budget_us < MinBudget) return false;

    SequenceStepEnables enables{};
    SequenceStepTimeouts timeouts{};
    getSequenceStepEnables(&enables);
    getSequenceStepTimeouts(&enables, &timeouts);

    uint32_t used = StartOverhead + EndOverhead;
    if (enables.tcc)        used += timeouts.msrc_dss_tcc_us + TccOverhead;
    if (enables.dss)        used += 2 * (timeouts.msrc_dss_tcc_us + DssOverhead);
    else if (enables.msrc)  used += timeouts.msrc_dss_tcc_us + MsrcOverhead;
    if (enables.pre_range)  used += timeouts.pre_range_us + PreRangeOverhead;

    if (!enables.final_range) return true;
    if (used > budget_us - FinalRangeOverhead) return false;

    uint32_t final_us    = budget_us - used - FinalRangeOverhead;
    uint32_t final_mclks = usToMclks(final_us, timeouts.final_range_vcsel_period_pclks);
    if (enables.pre_range) final_mclks += timeouts.pre_range_mclks;

    writeReg16(Reg::FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI, encodeTimeout(final_mclks));
    return true;
}

bool IV_Vl53l0x::getSpadInfo(uint8_t* count, bool* type_is_aperture) {
    writeReg(0x80, 0x01); writeReg(0xFF, 0x01); writeReg(0x00, 0x00);
    writeReg(0xFF, 0x06);
    writeReg(0x83, readReg8(0x83) | 0x04);
    writeReg(0xFF, 0x07); writeReg(0x81, 0x01);
    writeReg(0x80, 0x01);
    writeReg(0x94, 0x6B);
    writeReg(0x83, 0x00);

    int timeout = 100;
    while (readReg8(0x83) == 0x00) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (--timeout == 0) return false;
    }

    writeReg(0x83, 0x01);
    uint8_t tmp = readReg8(0x92);
    *count            = tmp & 0x7F;
    *type_is_aperture = (tmp >> 7) & 0x01;

    writeReg(0x81, 0x00);
    writeReg(0xFF, 0x06);
    writeReg(0x83, readReg8(0x83) & ~0x04);
    writeReg(0xFF, 0x01); writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00); writeReg(0x80, 0x00);
    return true;
}

bool IV_Vl53l0x::performSingleRefCalibration(uint8_t vhv_init_byte) {
    writeReg(Reg::SYSRANGE_START, 0x01 | vhv_init_byte);

    int timeout = 500;
    while ((readReg8(Reg::RESULT_INTERRUPT_STATUS) & 0x07) == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (--timeout == 0) return false;
    }

    writeReg(Reg::SYSTEM_INTERRUPT_CLEAR, 0x01);
    writeReg(Reg::SYSRANGE_START, 0x00);
    return true;
}

bool IV_Vl53l0x::init() {
    if (initialized_) return true;

    if (!i2c_manager_init()) {
        logger_.error("Failed to init shared I2C bus");
        return false;
    }

    configureXshut(true);

    // Sensores VL53L0X sempre sobem com o endereço padrão (0x29). Se um endereço
    // alternativo foi configurado, muda o endereço no hardware antes de prosseguir.
    if (config_.address != I2C_ADDR_VL53L0X_DEFAULT) {
        if (!i2c_manager_register_device(I2C_ADDR_VL53L0X_DEFAULT, config_.i2c_speed_hz, nullptr)) {
            logger_.error("Failed to register at default address for address change");
            return false;
        }
        uint8_t buf[2] = {0x8A, config_.address}; // reg 0x8A = I2C_SLAVE_DEVICE_ADDRESS
        if (!i2c_manager_write(I2C_ADDR_VL53L0X_DEFAULT, buf, 2)) {
            logger_.error("Failed to change I2C address to 0x{:02X}", config_.address);
            // Libera o handle de 0x29 para que o próximo sensor possa registrá-lo limpo.
            i2c_manager_unregister_device(I2C_ADDR_VL53L0X_DEFAULT);
            vTaskDelay(pdMS_TO_TICKS(20)); // Aguarda a recuperação automática do barramento pelo ESP-IDF
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Aguarda o sensor aplicar a mudança de endereço
        i2c_manager_unregister_device(I2C_ADDR_VL53L0X_DEFAULT);
    }

    if (!i2c_manager_register_device(config_.address, config_.i2c_speed_hz, &dev_handle_)) {
        logger_.error("Failed to register device at 0x{:02X}", config_.address);
        return false;
    }

    // Verificar Model ID
    uint8_t model_id = readReg8(Reg::IDENTIFICATION_MODEL_ID);
    if (model_id != 0xEE) {
        logger_.error("VL53L0X not found — Model ID = 0x{:02X} (expected 0xEE)", model_id);
        return false;
    }
    logger_.info("VL53L0X found (Model ID 0x{:02X})", model_id);

    // Configurar tensão 2V8 se necessário
    writeReg(Reg::VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV,
             readReg8(Reg::VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV) | 0x01);

    // DataInit
    writeReg(0x88, 0x00);  // I2C standard mode
    writeReg(0x80, 0x01);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x00);
    stop_variable_ = readReg8(0x91);
    writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00);
    writeReg(0x80, 0x00);

    // Desabilitar verificações de rate MSRC e PRE_RANGE
    writeReg(Reg::MSRC_CONFIG_CONTROL, readReg8(Reg::MSRC_CONFIG_CONTROL) | 0x12);

    // Limite mínimo de sinal: 0.25 MCPS
    writeReg16(Reg::FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT, 0x0020);

    writeReg(Reg::SYSTEM_SEQUENCE_CONFIG, 0xFF);

    // SPAD info
    uint8_t spad_count;
    bool    spad_aperture;
    if (!getSpadInfo(&spad_count, &spad_aperture)) {
        logger_.error("getSpadInfo failed");
        return false;
    }

    uint8_t ref_spad_map[6];
    readMulti(Reg::GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, 6);

    writeReg(0xFF, 0x01);
    writeReg(Reg::DYNAMIC_SPAD_REF_EN_START_OFFSET, 0x00);
    writeReg(Reg::DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD, 0x2C);
    writeReg(0xFF, 0x00);
    writeReg(Reg::GLOBAL_CONFIG_REF_EN_START_SELECT, 0xB4);

    uint8_t first_spad = spad_aperture ? 12 : 0;
    uint8_t enabled    = 0;
    for (uint8_t i = 0; i < 48; i++) {
        if (i < first_spad || enabled == spad_count)
            ref_spad_map[i / 8] &= ~(1 << (i % 8));
        else if ((ref_spad_map[i / 8] >> (i % 8)) & 0x1)
            enabled++;
    }
    writeMulti(Reg::GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, 6);

    // Tuning settings (Pololu / ST app note)
    writeReg(0xFF, 0x01); writeReg(0x00, 0x00);
    writeReg(0xFF, 0x00); writeReg(0x09, 0x00);
    writeReg(0x10, 0x00); writeReg(0x11, 0x00);
    writeReg(0x24, 0x01); writeReg(0x25, 0xFF);
    writeReg(0x75, 0x00);
    writeReg(0xFF, 0x01); writeReg(0x4E, 0x2C);
    writeReg(0x48, 0x00); writeReg(0x30, 0x20);
    writeReg(0xFF, 0x00); writeReg(0x30, 0x09);
    writeReg(0x54, 0x00); writeReg(0x31, 0x04);
    writeReg(0x32, 0x03); writeReg(0x40, 0x83);
    writeReg(0x46, 0x25); writeReg(0x60, 0x00);
    writeReg(0x27, 0x00); writeReg(0x50, 0x06);
    writeReg(0x51, 0x00); writeReg(0x52, 0x96);
    writeReg(0x56, 0x08); writeReg(0x57, 0x30);
    writeReg(0x61, 0x00); writeReg(0x62, 0x00);
    writeReg(0x64, 0x00); writeReg(0x65, 0x00);
    writeReg(0x66, 0xA0);
    writeReg(0xFF, 0x01); writeReg(0x22, 0x32);
    writeReg(0x47, 0x14); writeReg(0x49, 0xFF);
    writeReg(0x4A, 0x00);
    writeReg(0xFF, 0x00); writeReg(0x7A, 0x0A);
    writeReg(0x7B, 0x00); writeReg(0x78, 0x21);
    writeReg(0xFF, 0x01); writeReg(0x23, 0x34);
    writeReg(0x42, 0x00); writeReg(0x44, 0xFF);
    writeReg(0x45, 0x26); writeReg(0x46, 0x05);
    writeReg(0x40, 0x40); writeReg(0x0E, 0x06);
    writeReg(0x20, 0x1A); writeReg(0x43, 0x40);
    writeReg(0xFF, 0x00); writeReg(0x34, 0x03);
    writeReg(0x35, 0x44);
    writeReg(0xFF, 0x01); writeReg(0x31, 0x04);
    writeReg(0x4B, 0x09); writeReg(0x4C, 0x05);
    writeReg(0x4D, 0x04);
    writeReg(0xFF, 0x00); writeReg(0x44, 0x00);
    writeReg(0x45, 0x20); writeReg(0x47, 0x08);
    writeReg(0x48, 0x28); writeReg(0x67, 0x00);
    writeReg(0x70, 0x04); writeReg(0x71, 0x01);
    writeReg(0x72, 0xFE); writeReg(0x76, 0x00);
    writeReg(0x77, 0x00);
    writeReg(0xFF, 0x01); writeReg(0x0D, 0x01);
    writeReg(0xFF, 0x00); writeReg(0x80, 0x01);
    writeReg(0x01, 0xF8);
    writeReg(0xFF, 0x01); writeReg(0x8E, 0x01);
    writeReg(0x00, 0x01); writeReg(0xFF, 0x00);
    writeReg(0x80, 0x00);

    // Configurar GPIO interrupt: new sample ready, polarity active low
    writeReg(Reg::SYSTEM_INTERRUPT_CONFIG_GPIO, 0x04);
    writeReg(Reg::GPIO_HV_MUX_ACTIVE_HIGH,
             readReg8(Reg::GPIO_HV_MUX_ACTIVE_HIGH) & ~0x10);
    writeReg(Reg::SYSTEM_INTERRUPT_CLEAR, 0x01);

    // Timing budget
    uint32_t budget_us = config_.timing_budget_ms * 1000UL;
    if (budget_us < 20000) budget_us = 20000;
    setMeasurementTimingBudget(budget_us);

    // Desabilitar sequência DSS (não necessário para ranging simples)
    writeReg(Reg::SYSTEM_SEQUENCE_CONFIG, 0xE8);
    setMeasurementTimingBudget(budget_us);

    // Calibração VHV
    writeReg(Reg::SYSTEM_SEQUENCE_CONFIG, 0x01);
    if (!performSingleRefCalibration(0x40)) {
        logger_.error("VHV calibration failed");
        return false;
    }

    // Calibração de fase
    writeReg(Reg::SYSTEM_SEQUENCE_CONFIG, 0x02);
    if (!performSingleRefCalibration(0x00)) {
        logger_.error("Phase calibration failed");
        return false;
    }

    writeReg(Reg::SYSTEM_SEQUENCE_CONFIG, 0xE8);

    initialized_ = true;
    logger_.info("VL53L0X inicializado com sucesso");
    return true;
}

// ---------------------------------------------------------------------------
// readDistanceMm  —  single-shot: dispara UMA medição e aguarda o resultado
// ---------------------------------------------------------------------------
float IV_Vl53l0x::readDistanceMm() {
    if (!initialized_) return -1.0f;

    // Dispara uma única medição
    writeReg(0x80, 0x01); writeReg(0xFF, 0x01); writeReg(0x00, 0x00);
    writeReg(0x91, stop_variable_);
    writeReg(0x00, 0x01); writeReg(0xFF, 0x00); writeReg(0x80, 0x00);
    writeReg(Reg::SYSRANGE_START, 0x01);

    // Aguarda o hardware confirmar o início (bit 0 limpa quando a medição começa)
    int timeout = 100;
    while (readReg8(Reg::SYSRANGE_START) & 0x01) {
        vTaskDelay(1); // 1 tick = 1 ms (CONFIG_FREERTOS_HZ=1000)
        if (--timeout == 0) {
            logger_.warn("Timeout ao iniciar medição");
            return -1.0f;
        }
    }

    // Aguarda dado pronto (≈ timing_budget_ms)
    timeout = 500;
    while ((readReg8(Reg::RESULT_INTERRUPT_STATUS) & 0x07) == 0) {
        vTaskDelay(1); // 1 tick = 1 ms
        if (--timeout == 0) {
            logger_.warn("Timeout aguardando resultado");
            return -1.0f;
        }
    }

    // RESULT_RANGE_STATUS + 10 bytes = 0x14 + 0x0A = 0x1E
    uint16_t mm = readReg16(Reg::RESULT_RANGE_STATUS + 10);
    writeReg(Reg::SYSTEM_INTERRUPT_CLEAR, 0x01);
    return static_cast<float>(mm);
}
