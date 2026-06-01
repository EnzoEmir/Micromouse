#ifndef TELEMETRIA_HPP
#define TELEMETRIA_HPP

#include <atomic>
#include <cstdint>
#include <string>

#include "esp_err.h"
#include "envio_dados.hpp"
#include "maze.hpp"

/**
 * @brief Gerenciador da sessão de telemetria.
 *
 * Camada de orquestração sobre o módulo envio_dados: conecta o Wi-Fi, mantém o
 * relógio relativo ao início da corrida (timestamp_ms da especificação), decide
 * *quando* enviar (eventos e heartbeat de inatividade) e delega o POST de cada
 * pacote para as funções enviar_*() de envio_dados.
 *
 * Formato dos pacotes: ver src/firmware/telemetria.md.
 */
class Telemetria {
private:
    const char* TAG = "TELEMETRIA";
    std::string _server_url;
    int64_t _inicio_ms;                    // referência: instante de início da corrida (uptime)
    std::atomic<int64_t> _ultimo_envio_ms; // uptime do último envio (lido/escrito por várias tasks)
    int64_t _timeout_heartbeat_ms;

    // Uptime (desde o boot) em milissegundos.
    int64_t agora_ms() const;

    // Tempo relativo ao início da corrida, em milissegundos (campo timestamp_ms).
    int64_t timestamp_ms() const;

    // Em caso de sucesso, registra o instante do envio (usado pelo heartbeat).
    esp_err_t registrar(esp_err_t err);

public:
    /**
     * @brief Construtor.
     * @param url Endereço completo da API backend.
     * @param heartbeat_ms Tempo limite (ms) sem envios para disparar um heartbeat.
     */
    Telemetria(std::string url, int64_t heartbeat_ms = 1500);

    /**
     * @brief Conecta ao Wi-Fi (bloqueante), zera o relógio da corrida e envia o
     *        pacote de Configuração Inicial (tipo 0).
     * @param ssid SSID da rede Wi-Fi.
     * @param senha Senha da rede Wi-Fi.
     * @param labirinto Labirinto cuja dimensão é enviada no pacote inicial.
     * @param bateria Porcentagem de bateria no início (0 a 100).
     */
    void inicializar(const char* ssid, const char* senha,
                     const Labirinto& labirinto, int bateria);

    /**
     * @brief Pacote tipo 1: o robô entrou em uma nova célula.
     * @param x Coordenada X atual.
     * @param y Coordenada Y atual.
     * @param paredes Bitmask de paredes da célula (N=1, S=2, L=4, O=8).
     */
    esp_err_t movimento(int x, int y, uint8_t paredes);

    /**
     * @brief Pacote tipo 2: rota ótima calculada pelo flood fill.
     * @param rota Coordenadas consecutivas do trajeto ideal.
     * @param n Quantidade de coordenadas em `rota`.
     */
    esp_err_t rota_otimizada(const Labirinto::Coordenada* rota, uint16_t n);

    /**
     * @brief Pacote tipo 3: fim de corrida (consolidação).
     * @param sucesso true se alcançou o centro, false se falhou.
     * @param v_med Velocidade média final do percurso (m/s).
     * @param bateria Porcentagem de bateria no final (0 a 100).
     */
    esp_err_t fim_corrida(bool sucesso, float v_med, int bateria);

    /**
     * @brief Pacote tipo 5: alerta crítico de temperatura.
     * @param temp_c Temperatura atual (°C).
     */
    esp_err_t alerta_temperatura(float temp_c);

    /**
     * @brief Dispara um heartbeat (tipo 4) se o robô estiver ocioso há tempo
     *        suficiente (sem nenhum envio desde _timeout_heartbeat_ms).
     * @param bateria Porcentagem de bateria no momento (0 a 100).
     */
    void verificar_heartbeat(int bateria);
};

#endif
