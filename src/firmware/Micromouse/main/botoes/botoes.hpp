#pragma once

#include <cstdint>
#include "driver/gpio.h"

// Botao momentaneo ativo em nivel ALTO (liga o pino ao 3V3 quando pressionado).
// Usa o pull-down interno do ESP32 e faz debounce por software. Projetado para
// ser consultado em laco (polling); chame clicado() periodicamente.
class Botao {
public:
    // Tipo de acionamento reconhecido por clique(): distingue um TOQUE CURTO de
    // SEGURAR o botao. Permite operar dois comandos com um unico botao.
    enum class Clique { Nenhum, Curto, Longo };

    // tempo_debounce_ms: janela de estabilidade exigida para aceitar a mudanca.
    explicit Botao(gpio_num_t pino, uint32_t tempo_debounce_ms = 25);

    // Configura o GPIO como entrada com pull-down. Captura o nivel inicial para
    // nao gerar um "clique" falso no boot.
    void init();

    // Retorna true UMA unica vez por pressionamento (borda de subida ja
    // debounced: solto -> pressionado). Deve ser chamado repetidamente.
    bool clicado();

    // Classifica o acionamento (polling; chame repetidamente):
    //   Curto  -> retornado UMA vez ao SOLTAR, se o botao ficou pressionado por
    //             menos de ms_longo.
    //   Longo  -> retornado UMA vez enquanto o botao AINDA esta pressionado, no
    //             instante em que o tempo segurado cruza ms_longo.
    //   Nenhum -> nos demais ciclos.
    // Nao misture com clicado() no mesmo botao (ambos consomem a mesma borda).
    Clique clique(uint32_t ms_longo = 700);

    // Nivel logico estavel atual: true = pressionado.
    bool pressionado() const { return estavel_pressionado_; }

private:
    // Atualiza o estado debounced e devolve a borda deste ciclo: +1 pressionou,
    // -1 soltou, 0 sem mudanca. Base compartilhada por clicado() e clique().
    int atualizarBorda();

    gpio_num_t pino_;
    int64_t    tempo_debounce_us_;

    bool    estavel_pressionado_;  // estado debounced (true = pressionado)
    bool    ultimo_lido_;          // ultima leitura crua (true = pressionado)
    int64_t t_ultima_mudanca_us_;  // quando a leitura crua mudou pela ultima vez

    // Detector de toque-curto vs. segurar (usado por clique()).
    int64_t t_press_inicio_us_;    // quando o pressionamento atual comecou
    bool    longo_disparado_;      // ja emitiu Longo neste pressionamento
};
