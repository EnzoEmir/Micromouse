#ifndef TELEMETRIA_HPP
#define TELEMETRIA_HPP

#include <string>
#include <cstdint>
#include "esp_err.h"

class Telemetria {
private:
    const char* TAG = "TELEMETRIA";
    std::string _server_url;
    std::string _id_corrida;
    int64_t _ultimo_envio_ms;
    int64_t _timeout_heartbeat_ms;

    void inicializar_id_corrida();
    esp_err_t enviar_post(const char* json_string);

public:
    /**
     * @brief Construtor da classe de Telemetria.
     * @param url Endereço completo da API backend.
     * @param heartbeat_ms Tempo limite em milissegundos para disparar o heartbeat se o robô estiver parado.
     */
    Telemetria(std::string url, int64_t heartbeat_ms = 10000);

    /**
     * @brief Inicializa o armazenamento NVS, incrementa o ID da corrida e faz o Handshake inicial.
     */
    void inicializar();

    /**
     * @brief Formata e envia um pacote de dados via HTTP POST.
     * @param tipo Tipo do pacote ("inicio", "movimento", "fim", "heartbeat").
     * @param bateria Nível atual da bateria (0 a 100).
     * @param v_med Velocidade média na célula atual em cm/s.
     * @param direcao Direção cardinal ("N", "S", "L", "O").
     */
    void enviar_pacote(std::string tipo, int bateria, int v_med, std::string direcao);

    /**
     * @brief Verifica se o robô está ocioso e dispara um pacote do tipo heartbeat se necessário.
     */
    void verificar_heartbeat(int bateria_atual, std::string direcao_atual);
};

#endif