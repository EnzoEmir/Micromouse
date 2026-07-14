// Host mock of the espp::Ina226 driver. Only the surface used by Battery is
// implemented; readings come from test-controlled globals.
#pragma once

#include <cstdint>
#include <functional>
#include <system_error>

namespace espp {

// Test-controlled latest readings (defined in ina226_mock.cpp).
struct Ina226MockState {
    float voltage = 0.0f;
    float current = 0.0f;
    float power = 0.0f;
};
Ina226MockState& ina226_mock_state();

inline void mock_ina226_set(float voltage, float current, float power) {
    auto& s = ina226_mock_state();
    s.voltage = voltage;
    s.current = current;
    s.power = power;
}

class Ina226 {
public:
    using write_fn = std::function<bool(uint8_t, const uint8_t*, size_t)>;
    using read_register_fn = std::function<bool(uint8_t, uint8_t, uint8_t*, size_t)>;

    struct Config {
        uint8_t device_address = 0x40;
        float current_lsb = 0.001f;
        float shunt_resistance_ohms = 0.1f;
        write_fn write{nullptr};
        read_register_fn read_register{nullptr};
        bool auto_init = true;
    };

    explicit Ina226(const Config& config) : config_(config) {}

    float bus_voltage_volts(std::error_code& ec) const {
        ec.clear();
        return ina226_mock_state().voltage;
    }
    float current_amps(std::error_code& ec) const {
        ec.clear();
        return ina226_mock_state().current;
    }
    float power_watts(std::error_code& ec) {
        ec.clear();
        return ina226_mock_state().power;
    }

    // Diagnostico: por padrao devolve os IDs de um INA226 genuino (TI/0x2260) e
    // o shunt derivado da corrente mockada (Vshunt = I * Rshunt).
    uint16_t manufacturer_id(std::error_code& ec) const { ec.clear(); return 0x5449; }
    uint16_t die_id(std::error_code& ec) const { ec.clear(); return 0x2260; }
    float shunt_voltage_volts(std::error_code& ec) const {
        ec.clear();
        return ina226_mock_state().current * config_.shunt_resistance_ohms;
    }
    bool calibrate(float, float, std::error_code& ec) { ec.clear(); return true; }

private:
    Config config_;
};

}  // namespace espp
