#ifndef ENVIO_DADOS_HPP
#define ENVIO_DADOS_HPP

#include <cstdint>
#include "esp_err.h"
#include "maze.hpp"

/**
 * @brief Tipo do pacote de telemetria (campo "tipo" do JSON).
 *
 * Os valores inteiros e o layout de cada pacote seguem a especificação em
 * src/firmware/telemetria.md. O campo "tipo" é sempre o primeiro do JSON,
 * permitindo ao web identificar o pacote sem inspecionar o restante.
 */
enum class TipoPacote : int {
    ConfiguracaoInicial = 0,  // Disparado uma vez na largada
    Movimentacao        = 1,  // Ao entrar em uma nova célula (descoberta de paredes)
    RotaOtimizada       = 2,  // Uma vez, após o cálculo do flood fill
    FimDeCorrida        = 3,  // Uma vez, ao terminar/falhar
    Heartbeat           = 4,  // Periódico, enquanto o ESP32 estiver ativo
    AlertaTemperatura   = 5,  // Temperatura acima do limiar crítico
};

/**
 * @brief Pacote 0 — Configuração Inicial.
 *
 * { "tipo":0, "timestamp_ms":..., "dimensao":..., "lado_largada":..., "bateria":... }
 *
 * @param url          Endereço completo da API backend.
 * @param timestamp_ms Tempo relativo ao início da corrida (ms).
 * @param dimensao     Tamanho do labirinto (4, 8 ou 16).
 * @param lado_largada Lado em que o mapeamento começa: "esquerda" ou "direita".
 * @param bateria      Porcentagem estimada de bateria no início (0 a 100).
 */
esp_err_t enviar_configuracao_inicial(const char* url, int64_t timestamp_ms,
                                      int dimensao, const char* lado_largada,
                                      int bateria);

/**
 * @brief Pacote 1 — Movimentação / Descoberta de Paredes.
 *
 * { "tipo":1, "timestamp_ms":..., "x":..., "y":..., "w":... }
 *
 * @param url          Endereço completo da API backend.
 * @param timestamp_ms Tempo relativo ao início da corrida (ms).
 * @param x            Coordenada X atual (origem [0,0] no canto inferior esquerdo).
 * @param y            Coordenada Y atual (cresce para o Norte).
 * @param w            Bitmask de paredes da célula (N=1, S=2, L=4, O=8).
 */
esp_err_t enviar_movimentacao(const char* url, int64_t timestamp_ms,
                              int x, int y, uint8_t w);

/**
 * @brief Pacote 2 — Rota Otimizada.
 *
 * { "tipo":2, "timestamp_ms":..., "rota":[[x,y], ...] }
 *
 * @param url          Endereço completo da API backend.
 * @param timestamp_ms Tempo relativo ao início da corrida (ms).
 * @param rota         Coordenadas consecutivas do início ao fim do trajeto ideal.
 * @param n            Quantidade de coordenadas em `rota`.
 */
esp_err_t enviar_rota_otimizada(const char* url, int64_t timestamp_ms,
                                const Labirinto::Coordenada* rota, uint16_t n);

/**
 * @brief Pacote 3 — Fim de Corrida / Consolidação.
 *
 * { "tipo":3, "timestamp_ms":..., "sucesso":..., "v_med":..., "bateria":... }
 *
 * @param url          Endereço completo da API backend.
 * @param timestamp_ms Tempo relativo ao início da corrida (ms).
 * @param sucesso      true se alcançou o centro autonomamente, false se falhou.
 * @param v_med        Velocidade média final do percurso (m/s).
 * @param bateria      Porcentagem estimada de bateria no final (0 a 100).
 */
esp_err_t enviar_fim_corrida(const char* url, int64_t timestamp_ms,
                             bool sucesso, float v_med, int bateria);

/**
 * @brief Pacote 4 — Heartbeat.
 *
 * { "tipo":4, "timestamp_ms":..., "bateria":... }
 *
 * @param url          Endereço completo da API backend.
 * @param timestamp_ms Tempo relativo ao início da corrida (ms).
 * @param bateria      Porcentagem estimada de bateria no momento do envio (0 a 100).
 */
esp_err_t enviar_heartbeat(const char* url, int64_t timestamp_ms, int bateria);

/**
 * @brief Pacote 5 — Alerta Crítico de Temperatura.
 *
 * { "tipo":5, "timestamp_ms":..., "temp_c":... }
 *
 * @param url          Endereço completo da API backend.
 * @param timestamp_ms Tempo relativo ao início da corrida (ms).
 * @param temp_c       Temperatura atual medida pelo sensor (°C).
 */
esp_err_t enviar_alerta_temperatura(const char* url, int64_t timestamp_ms, float temp_c);

#endif // ENVIO_DADOS_HPP
