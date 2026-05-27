#ifndef ENVIO_DADOS_HPP
#define ENVIO_DADOS_HPP

#include <cstdint>
#include "esp_err.h"
#include "maze.hpp"

/**
 * @brief Tipo do pacote de telemetria, indicando o evento que o originou.
 */
enum class TipoEnvio {
    AvancoTile,        // Robô avançou uma célula (tile)
    Heartbeat,         // Pacote periódico de manutenção da sessão
    InicioMapeamento,  // Início da fase de mapeamento do labirinto
    FinalMapeamento,   // Fim da fase de mapeamento do labirinto
    InicioCorrida,     // Início da corrida pelo melhor caminho
    FinalCorrida,      // Fim da corrida
};

/**
 * @brief Converte um TipoEnvio para a string usada no campo "tipo" do JSON.
 */
const char* tipo_envio_para_string(TipoEnvio tipo);

/**
 * @brief Conjunto de dados de telemetria enviado ao backend.
 *
 * A matriz do labirinto não entra aqui: ela é lida diretamente do objeto
 * Labirinto passado para enviar_dados_sensores().
 */
struct DadosEnvio {
    TipoEnvio tipo;              // Tipo/evento do pacote
    float velocidade_media_cms;  // Velocidade média na célula atual (cm/s)
    const char* direcao;         // Direção observada ("N", "S", "L", "O")
    float temperatura;           // Temperatura do IMU (°C)
    int soc;                     // Estado de carga da bateria (0 a 100 %)
    int64_t timestamp_ms;        // Referência temporal da leitura (ms)
};

/**
 * @brief Monta um JSON plano com os dados de telemetria + a matriz do
 *        labirinto e envia via HTTP POST para a URL informada.
 *
 * A matriz é serializada como um array 2D (linha x coluna) de bitmasks de
 * paredes por célula (N=1, S=2, L=4, O=8).
 *
 * @param url       Endereço completo da API backend.
 * @param dados     Estrutura com os dados escalares de telemetria.
 * @param labirinto Referência ao labirinto cuja matriz será serializada.
 * @return ESP_OK em caso de sucesso, ou um código de erro do esp_http_client.
 */
esp_err_t enviar_dados_sensores(const char* url,
                                const DadosEnvio& dados,
                                const Labirinto& labirinto);

#endif // ENVIO_DADOS_HPP
