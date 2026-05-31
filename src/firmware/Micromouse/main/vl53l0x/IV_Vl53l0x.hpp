#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "logger.hpp"
#include "i2c_manager.hpp"

class IV_Vl53l0x {
public:
    enum class Position {
        FRONT,
        FRONT_LEFT,
        FRONT_RIGHT,
        LEFT,
        RIGHT,
        BACK,
        CUSTOM,
    };

    struct Config {
        Position   position          = Position::FRONT;
        uint8_t    address           = I2C_ADDR_VL53L0X_DEFAULT;
        gpio_num_t xshut_pin         = GPIO_NUM_NC;
        uint32_t   i2c_speed_hz      = I2C_MANAGER_DEFAULT_SPEED_HZ;
        uint32_t   timing_budget_ms  = 20;
        espp::Logger::Verbosity log_level = espp::Logger::Verbosity::INFO;
    };

    explicit IV_Vl53l0x(const Config& config);

    bool  init();
    float readDistanceMm();

    // Controle do pino XSHUT (útil em setups multi-sensor)
    void enable();
    void disable();

    Position         position()      const { return config_.position; }
    uint8_t          address()       const { return config_.address; }
    bool             isInitialized() const { return initialized_; }
    std::string_view positionName()  const;

private:
    Config       config_;
    std::string  log_tag_;
    espp::Logger logger_;

    i2c_master_dev_handle_t dev_handle_ = nullptr;
    bool    initialized_   = false;
    uint8_t stop_variable_ = 0;

    void configureXshut(bool high);

    // I2C helpers (8-bit register address, VL53L0X protocol)
    bool     writeReg(uint8_t reg, uint8_t val);
    bool     writeReg16(uint8_t reg, uint16_t val);
    bool     writeMulti(uint8_t reg, const uint8_t* data, size_t len);
    uint8_t  readReg8(uint8_t reg);
    uint16_t readReg16(uint8_t reg);
    bool     readMulti(uint8_t reg, uint8_t* data, size_t len);

    // Helpers de inicialização
    bool getSpadInfo(uint8_t* count, bool* type_is_aperture);
    bool setMeasurementTimingBudget(uint32_t budget_us);
    bool performSingleRefCalibration(uint8_t vhv_init_byte);

    struct SequenceStepEnables {
        bool tcc, dss, msrc, pre_range, final_range;
    };
    struct SequenceStepTimeouts {
        uint8_t  pre_range_vcsel_period_pclks;
        uint8_t  final_range_vcsel_period_pclks;
        uint16_t msrc_dss_tcc_mclks;
        uint16_t pre_range_mclks;
        uint16_t final_range_mclks;
        uint32_t msrc_dss_tcc_us;
        uint32_t pre_range_us;
        uint32_t final_range_us;
    };
    void getSequenceStepEnables(SequenceStepEnables* e);
    void getSequenceStepTimeouts(const SequenceStepEnables* e, SequenceStepTimeouts* t);

    static uint16_t decodeTimeout(uint16_t val);
    static uint16_t encodeTimeout(uint32_t mclks);
    static uint32_t mclksToUs(uint16_t mclks, uint8_t vcsel_pclks);
    static uint32_t usToMclks(uint32_t us, uint8_t vcsel_pclks);
};
