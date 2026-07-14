#ifndef WIFI_HPP
#define WIFI_HPP

#include "esp_err.h"

/**
 * @brief Inicializa o Wi-Fi em modo Station. A conexão (e as reconexões em
 *        caso de queda) acontecem em background via eventos; use
 *        wifi_is_connected() para saber o estado atual.
 * @param ssid Nome da rede Wi-Fi
 * @param password Senha da rede Wi-Fi
 * @param timeout_ms Quanto tempo esperar pela primeira conexão antes de
 *        retornar. 0 = não espera (retorna imediatamente e conecta em
 *        background). Nunca bloqueia para sempre.
 */
void wifi_init_sta(const char* ssid, const char* password, int timeout_ms = 10000);

/**
 * @brief Retorna se o Wi-Fi está atualmente conectado e com IP.
 */
bool wifi_is_connected(void);

#endif