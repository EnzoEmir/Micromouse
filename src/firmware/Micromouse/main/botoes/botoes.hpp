#pragma once

#include <cstdint>
#include "driver/gpio.h"

// Botao momentaneo ativo em nivel baixo (liga o pino ao GND quando pressionado).
// Usa o pull-up interno do ESP32 e faz debounce por software. Projetado para ser
// consultado em laco (polling); chame clicado() periodicamente.
class Botao {
public:
    // tempo_debounce_ms: janela de estabilidade exigida para aceitar a mudanca.
    explicit Botao(gpio_num_t pino, uint32_t tempo_debounce_ms = 25);

    // Configura o GPIO como entrada com pull-up. Captura o nivel inicial para
    // nao gerar um "clique" falso no boot.
    void init();

    // Retorna true UMA unica vez por pressionamento (borda de descida ja
    // debounced: solto -> pressionado). Deve ser chamado repetidamente.
    bool clicado();

    // Nivel logico estavel atual: true = pressionado.
    bool pressionado() const { return estavel_pressionado_; }

private:
    gpio_num_t pino_;
    int64_t    tempo_debounce_us_;

    bool    estavel_pressionado_;  // estado debounced (true = pressionado)
    bool    ultimo_lido_;          // ultima leitura crua (true = pressionado)
    int64_t t_ultima_mudanca_us_;  // quando a leitura crua mudou pela ultima vez
};
