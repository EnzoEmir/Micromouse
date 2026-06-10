#pragma once

#include "driver/gpio.h"

// Barramento I2C compartilhado
#define I2C_SDA_PIN GPIO_NUM_21
#define I2C_SCL_PIN GPIO_NUM_22

// Standby da Ponte H (Ajuste caso use outro pino físico)
#define MOTOR_STBY_PIN        GPIO_NUM_2

// Motor Esquerdo (Lado A)
#define MOTOR_LEFT_PWM_PIN    GPIO_NUM_32
#define MOTOR_LEFT_IN1_PIN    GPIO_NUM_33
#define MOTOR_LEFT_IN2_PIN    GPIO_NUM_16
// Conector C1
#define MOTOR_LEFT_ENC_A_PIN  GPIO_NUM_34 // C1-01
#define MOTOR_LEFT_ENC_B_PIN  GPIO_NUM_35 // C1-02

// Motor Direito (Lado B)
#define MOTOR_RIGHT_PWM_PIN   GPIO_NUM_4
#define MOTOR_RIGHT_IN1_PIN   GPIO_NUM_17
#define MOTOR_RIGHT_IN2_PIN   GPIO_NUM_13
// Conector C2
#define MOTOR_RIGHT_ENC_A_PIN GPIO_NUM_36 // C2-01 (VP)
#define MOTOR_RIGHT_ENC_B_PIN GPIO_NUM_39 // C2-02 (VN)

// Sensores ToF (VL53L0X) — pinos XSHUT
// ATENÇÃO: verifique os pinos abaixo contra o esquemático antes de soldar
#define TOF_FRONT_XSHUT_PIN        GPIO_NUM_25
#define TOF_FRONT_LEFT_XSHUT_PIN   GPIO_NUM_27
#define TOF_FRONT_RIGHT_XSHUT_PIN  GPIO_NUM_14
#define TOF_LEFT_XSHUT_PIN         GPIO_NUM_18
#define TOF_RIGHT_XSHUT_PIN        GPIO_NUM_26