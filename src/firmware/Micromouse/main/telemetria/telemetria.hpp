#ifndef TELEMETRIA_HPP
#define TELEMETRIA_HPP

#include <string>
#include <cstdint>
#include "esp_err.h"
#include "envio_dados.hpp"
#include "maze.hpp"

/**
 * @brief Gerenciador da sessão de telemetria.
 *
 * Integra os três módulos do envio: conecta o Wi-Fi, decide *quando* enviar
 * (eventos e heartbeat de inatividade) e delega o POST em si para
 * enviar_dados_sensores() (módulo envio_dados).
 */
class Telemetria {
private:
    const char* TAG = "TELEMETRIA";
    std::string _server_url;
    int64_t _ultimo_envio_ms;
    int64_t _timeout_heartbeat_ms;

    // Tempo atual (uptime) em milissegundos.
    int64_t agora_ms() const;

public:
    /**
     * @brief Construtor da classe de Telemetria.
     * @param url Endereço completo da API backend.
     * @param heartbeat_ms Tempo limite (ms) para disparar o heartbeat com o robô parado.
     */
    Telemetria(std::string url, int64_t heartbeat_ms = 10000);

    /**
     * @brief Conecta ao Wi-Fi (bloqueante) e envia o pacote de início de mapeamento.
     * @param ssid SSID da rede Wi-Fi.
     * @param senha Senha da rede Wi-Fi.
     * @param labirinto Labirinto cuja matriz acompanha o pacote inicial.
     */
    void inicializar(const char* ssid, const char* senha, const Labirinto& labirinto);

    /**
     * @brief Monta o pacote e o envia via HTTP POST, respeitando o estado do Wi-Fi.
     *
     * Não envia (e retorna ESP_FAIL) caso o Wi-Fi esteja desconectado. Em caso de
     * sucesso, atualiza o instante do último envio (usado pelo heartbeat).
     *
     * @param tipo Tipo/evento do pacote.
     * @param labirinto Labirinto cuja matriz será serializada.
     * @param soc Estado de carga da bateria (0 a 100 %).
     * @param direcao Direção observada ("N", "S", "L", "O").
     * @param velocidade_media_cms Velocidade média na célula atual (cm/s).
     * @param temperatura Temperatura do IMU (°C).
     */
    esp_err_t enviar(TipoEnvio tipo,
                     const Labirinto& labirinto,
                     int soc,
                     const char* direcao,
                     float velocidade_media_cms,
                     float temperatura);

    /**
     * @brief Dispara um pacote heartbeat se o robô estiver ocioso há tempo suficiente.
     */
    void verificar_heartbeat(const Labirinto& labirinto,
                             int soc,
                             const char* direcao,
                             float temperatura);
};

#endif
