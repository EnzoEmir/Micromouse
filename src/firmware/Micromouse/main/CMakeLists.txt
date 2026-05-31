idf_component_register(
    SRCS
        "app/main.cpp"
        "app/combined_test.cpp"
        "app/paralel_test.cpp"
        "i2c_manager.cpp"
        "battery/battery.cpp"
        "motor/motor.cpp"
        "vl53l0x/IV_Vl53l0x.cpp"
        "imu/imu.cpp"
        "maze/maze.cpp"
        "telemetria/telemetria.cpp"
        "wifi/wifi.cpp"
        "envio_dados/envio_dados.cpp"
    INCLUDE_DIRS "." "app" "battery" "motor" "vl53l0x" "imu" "maze" "telemetria" "wifi" "envio_dados"
    REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_driver_pcnt esp_driver_uart nvs_flash esp_timer truita__mpu9250 esp_http_client espressif__cjson esp_wifi esp_event esp_netif
)