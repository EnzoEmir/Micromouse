// Tests for the Motor module: H-bridge direction logic, PWM duty clamping, and
// encoder readback. The GPIO/LEDC/PCNT peripherals are mocked and inspected.
#include "framework.hpp"

#include "motor/motor.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"

// Pin assignment for the device under test.
static const int PWM = 32, AIN1 = 33, AIN2 = 16, STBY = 2, ENCA = 34, ENCB = 35;
static const int CH = LEDC_CHANNEL_0;

static Motor makeMotor() {
    mock_gpio_reset();
    mock_ledc_reset();
    mock_pcnt_reset();
    return Motor(PWM, AIN1, AIN2, STBY, ENCA, ENCB, (ledc_channel_t)CH);
}

TEST_CASE(begin_releases_standby) {
    Motor m = makeMotor();
    m.begin();
    CHECK_EQ(mock_gpio_level(STBY), 1);
}

TEST_CASE(forward_sets_direction_and_duty) {
    Motor m = makeMotor();
    m.setSpeed(500);
    CHECK_EQ(mock_gpio_level(AIN1), 1);
    CHECK_EQ(mock_gpio_level(AIN2), 0);
    CHECK_EQ(mock_ledc_duty(CH), 500);
}

TEST_CASE(reverse_sets_direction_and_abs_duty) {
    Motor m = makeMotor();
    m.setSpeed(-400);
    CHECK_EQ(mock_gpio_level(AIN1), 0);
    CHECK_EQ(mock_gpio_level(AIN2), 1);
    CHECK_EQ(mock_ledc_duty(CH), 400);  // magnitude
}

TEST_CASE(zero_speed_brakes) {
    Motor m = makeMotor();
    m.setSpeed(0);
    // Brake = both high.
    CHECK_EQ(mock_gpio_level(AIN1), 1);
    CHECK_EQ(mock_gpio_level(AIN2), 1);
    CHECK_EQ(mock_ledc_duty(CH), 0);
}

TEST_CASE(duty_is_clamped_to_1023_forward) {
    Motor m = makeMotor();
    m.setSpeed(5000);
    CHECK_EQ(mock_gpio_level(AIN1), 1);
    CHECK_EQ(mock_gpio_level(AIN2), 0);
    CHECK_EQ(mock_ledc_duty(CH), 1023);
}

TEST_CASE(duty_is_clamped_to_1023_reverse) {
    Motor m = makeMotor();
    m.setSpeed(-5000);
    CHECK_EQ(mock_gpio_level(AIN1), 0);
    CHECK_EQ(mock_gpio_level(AIN2), 1);
    CHECK_EQ(mock_ledc_duty(CH), 1023);
}

TEST_CASE(encoder_count_reads_through) {
    Motor m = makeMotor();
    m.begin();
    mock_pcnt_set_count(1234);
    CHECK_EQ(m.getEncoderCount(), 1234);
    mock_pcnt_set_count(-77);
    CHECK_EQ(m.getEncoderCount(), -77);
}

TEST_MAIN()
