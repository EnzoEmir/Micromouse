#include "botoes/botoes.hpp"

#include "esp_timer.h"

Botao::Botao(gpio_num_t pino, uint32_t tempo_debounce_ms)
    : pino_(pino),
      tempo_debounce_us_((int64_t)tempo_debounce_ms * 1000),
      estavel_pressionado_(false),
      ultimo_lido_(false),
      t_ultima_mudanca_us_(0) {}

void Botao::init() {
    gpio_config_t cfg = {};
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pin_bit_mask = (1ULL << pino_);
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    // Nivel cru inicial (ativo-baixo: nivel 0 => pressionado). Captura o estado
    // de repouso para nao disparar um clique falso na primeira leitura.
    const bool pressionado = (gpio_get_level(pino_) == 0);
    estavel_pressionado_ = pressionado;
    ultimo_lido_         = pressionado;
    t_ultima_mudanca_us_ = esp_timer_get_time();
}

bool Botao::clicado() {
    const bool lido = (gpio_get_level(pino_) == 0); // true = pressionado
    const int64_t agora = esp_timer_get_time();

    // Reinicia o cronometro de estabilidade sempre que a leitura crua muda.
    if (lido != ultimo_lido_) {
        ultimo_lido_ = lido;
        t_ultima_mudanca_us_ = agora;
        return false;
    }

    // Leitura estavel por tempo suficiente: confirma o novo estado.
    if (lido != estavel_pressionado_ &&
        (agora - t_ultima_mudanca_us_) >= tempo_debounce_us_) {
        estavel_pressionado_ = lido;
        // Borda de descida (solto -> pressionado) conta como um clique.
        if (estavel_pressionado_) return true;
    }
    return false;
}
