#ifndef WIFI_HPP
#define WIFI_HPP

#include "esp_err.h"

/**
 * @brief Inicializa o Wi-Fi em modo Station e bloqueia até conectar.
 * @param ssid Nome da rede Wi-Fi
 * @param password Senha da rede Wi-Fi
 */
void wifi_init_sta(const char* ssid, const char* password);

/**
 * @brief Retorna se o Wi-Fi está atualmente conectado e com IP.
 */
bool wifi_is_connected(void);

#endif