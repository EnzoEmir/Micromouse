// =============================================================================
//  imu.h - Interface do modulo de IMU (ESP32 + MPU-9250)
// =============================================================================
// Interface publica do IMU: leitura, calibracao e dados do sensor.
// MPU-9250 via I2C (ESP-IDF v6). Biblioteca:
// https://components.espressif.com/components/truita/mpu9250
// =============================================================================

#ifndef IMU_H
#define IMU_H

// Endereco I2C do MPU-9250 (padrao 0x68).
#include "i2c_manager.hpp"
#define IMU_ENDERECO_I2C  I2C_ADDR_MPU9250_PRIMARY

// -----------------------------------------------------------------------------
//  Parametros de operacao
// -----------------------------------------------------------------------------
#define IMU_FAIXA_ACCEL   MPU9250::ACCEL_RANGE_4G
#define IMU_FAIXA_GYRO    MPU9250::GYRO_RANGE_500DPS
#define IMU_FILTRO_DLPF   MPU9250::DLPF_BANDWIDTH_41HZ
#define IMU_SRD  19   // Taxa de amostragem para 50 Hz. (1000/numero+1hz)

// -----------------------------------------------------------------------------
//  Estruturas de dados
// -----------------------------------------------------------------------------

// Estado instantaneo do sensor.
struct DadosIMU {
    float accel_x, accel_y, accel_z;   // Aceleracao linear (m/s^2).
    float gyro_x, gyro_y, gyro_z;      // Velocidade angular (rad/s).
    float mag_x, mag_y, mag_z;         // Campo magnetico (uT).
    float temperatura;                 // Temperatura do silicio (C).
    unsigned long timestamp_ms;        // Timestamp da leitura.
};

// Bias e escalas de calibracao.
struct CalibracaoIMU {
    float gyro_bias_x, gyro_bias_y, gyro_bias_z;
    float accel_bias_x, accel_bias_y, accel_bias_z;
    float accel_escala_x, accel_escala_y, accel_escala_z;
    float mag_bias_x, mag_bias_y, mag_bias_z;
    float mag_escala_x, mag_escala_y, mag_escala_z;
    bool calibrado;
};

// -----------------------------------------------------------------------------
//  Assinaturas publicas
// -----------------------------------------------------------------------------
bool imu_init();
bool imu_update();
DadosIMU imu_get_dados();
CalibracaoIMU imu_get_calibracao();
bool imu_calibrar_gyro();
bool imu_calibrar_accel();
bool imu_calibrar_mag();
void imu_resetar_calibracao();
void imu_imprimir_dados();
void imu_processar_serial();

#endif // IMU_H