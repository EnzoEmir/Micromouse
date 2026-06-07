// Tests for the Battery module: state-of-charge estimation (voltage lookup +
// coulomb counting + rest correction) and the moving-average/LPF filter.
//
// The INA226 driver and the I2C bus are mocked; readings are injected via
// espp::mock_ina226_set(). The clock is the virtual esp_timer mock.
#include "framework.hpp"

#include "battery/battery.hpp"
#include "esp_timer.h"
#include "ina226.hpp"

// Convenience: build a Battery initialised at a given resting voltage.
static Battery makeBattery(float volts, float amps = 0.0f, float watts = 0.0f) {
    espp::mock_ina226_set(volts, amps, watts);
    mock_timer_reset();
    Battery b;
    b.init();  // seeds filters and soc_ from the current readings at t=0
    return b;
}

// --- voltageToSOC (probed through getSOC right after init) ------------------
TEST_CASE(soc_from_voltage_endpoints) {
    Battery low = makeBattery(6.6f);
    CHECK_FLOAT_EQ(low.getSOC(), 0.0f, 1e-3);

    Battery full = makeBattery(8.4f);
    CHECK_FLOAT_EQ(full.getSOC(), 100.0f, 1e-3);

    Battery mid = makeBattery(7.5f);
    CHECK_FLOAT_EQ(mid.getSOC(), 50.0f, 1e-3);
}

TEST_CASE(soc_from_voltage_is_clamped) {
    Battery below = makeBattery(5.0f);  // under the 6.6 V floor
    CHECK_FLOAT_EQ(below.getSOC(), 0.0f, 1e-3);

    Battery above = makeBattery(9.0f);  // over the 8.4 V ceiling
    CHECK_FLOAT_EQ(above.getSOC(), 100.0f, 1e-3);
}

// --- raw passthrough -------------------------------------------------------
TEST_CASE(getters_reflect_sensor) {
    espp::mock_ina226_set(7.4f, 1.5f, 11.1f);
    Battery b = makeBattery(7.4f, 1.5f, 11.1f);
    CHECK_FLOAT_EQ(b.getVoltage(), 7.4f, 1e-3);
    CHECK_FLOAT_EQ(b.getCurrent(), 1.5f, 1e-3);
    CHECK_FLOAT_EQ(b.getPower(), 11.1f, 1e-3);
}

// --- coulomb counting ------------------------------------------------------
// capacity = 2.0 Ah = 7200 As. dSOC% = current * dt / 7200 * 100.
TEST_CASE(coulomb_counting_integrates_current) {
    Battery b = makeBattery(7.5f);  // start at 50%
    CHECK_FLOAT_EQ(b.getSOC(), 50.0f, 1e-3);

    // 7.2 A for 100 s -> 7.2*100/7200*100 = 10%.
    espp::mock_ina226_set(7.5f, 7.2f, 54.0f);
    mock_timer_advance_us(100 * 1000000LL);
    b.update();
    CHECK_FLOAT_EQ(b.getSOC(), 60.0f, 1e-2);
}

TEST_CASE(soc_is_clamped_high_and_low) {
    Battery hi = makeBattery(7.5f);  // 50%
    espp::mock_ina226_set(7.5f, 1000.0f, 0.0f);  // absurd charge current
    mock_timer_advance_us(100 * 1000000LL);
    hi.update();
    CHECK_FLOAT_EQ(hi.getSOC(), 100.0f, 1e-3);

    Battery lo = makeBattery(7.5f);  // 50%
    espp::mock_ina226_set(7.5f, -1000.0f, 0.0f);  // absurd discharge
    mock_timer_advance_us(100 * 1000000LL);
    lo.update();
    CHECK_FLOAT_EQ(lo.getSOC(), 0.0f, 1e-3);
}

TEST_CASE(rest_voltage_correction_nudges_soc) {
    Battery b = makeBattery(7.5f);  // 50%
    // At rest (|I| < 0.05 A) the SOC drifts toward voltageToSOC(voltage) by
    // alpha = 0.01. Set voltage to imply 100% and current ~0.
    espp::mock_ina226_set(8.4f, 0.0f, 0.0f);
    mock_timer_advance_us(1 * 1000000LL);
    b.update();
    // 50 + 0.01*(100-50) = 50.5 ; coulomb term is 0 because current is 0.
    CHECK_FLOAT_EQ(b.getSOC(), 50.5f, 1e-3);
}

// --- filter ---------------------------------------------------------------
TEST_CASE(filter_constant_signal_is_stable) {
    Battery b = makeBattery(7.5f, 2.0f, 15.0f);
    // With a constant input equal to the seed, the filter output stays put.
    CHECK_FLOAT_EQ(b.getVoltageFiltered(), 7.5f, 1e-3);
    CHECK_FLOAT_EQ(b.getCurrentFiltered(), 2.0f, 1e-3);
    CHECK_FLOAT_EQ(b.getPowerFiltered(), 15.0f, 1e-3);
}

TEST_CASE(filter_converges_to_new_value) {
    Battery b = makeBattery(7.5f);  // seeds voltage filter at 7.5
    espp::mock_ina226_set(8.0f, 0.0f, 0.0f);
    float v = 0.0f;
    for (int i = 0; i < 500; ++i) v = b.getVoltageFiltered();
    CHECK_FLOAT_EQ(v, 8.0f, 1e-2);  // should have converged to the new level
}

TEST_MAIN()
