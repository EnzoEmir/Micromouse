#include "botoes/botoes.hpp"

#include "esp_timer.h"

Botao::Botao(gpio_num_t pino, uint32_t tempo_debounce_ms)
    : pino_(pino),
      tempo_debounce_us_((int64_t)tempo_debounce_ms * 1000),
      estavel_pressionado_(false),
      ultimo_lido_(false),
      t_ultima_mudanca_us_(0),
      t_press_inicio_us_(0),
      longo_disparado_(false) {}

void Botao::init() {
    gpio_config_t cfg = {};
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pin_bit_mask = (1ULL << pino_);
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    // Nivel cru inicial (ativo-alto: nivel 1 => pressionado). Captura o estado
    // de repouso para nao disparar um clique falso na primeira leitura.
    const bool pressionado = (gpio_get_level(pino_) == 1);
    estavel_pressionado_ = pressionado;
    ultimo_lido_         = pressionado;
    t_ultima_mudanca_us_ = esp_timer_get_time();
}

// Atualiza o estado debounced e devolve a borda deste ciclo:
//   +1 = solto -> pressionado | -1 = pressionado -> solto | 0 = sem mudanca.
int Botao::atualizarBorda() {
    const bool lido = (gpio_get_level(pino_) == 1); // true = pressionado
    const int64_t agora = esp_timer_get_time();

    // Reinicia o cronometro de estabilidade sempre que a leitura crua muda.
    if (lido != ultimo_lido_) {
        ultimo_lido_ = lido;
        t_ultima_mudanca_us_ = agora;
        return 0;
    }

    // Leitura estavel por tempo suficiente: confirma o novo estado.
    if (lido != estavel_pressionado_ &&
        (agora - t_ultima_mudanca_us_) >= tempo_debounce_us_) {
        estavel_pressionado_ = lido;
        return estavel_pressionado_ ? +1 : -1;
    }
    return 0;
}

bool Botao::clicado() {
    // So a borda de subida (solto -> pressionado) conta como um clique.
    return atualizarBorda() > 0;
}

Botao::Clique Botao::clique(uint32_t ms_longo) {
    const int     borda = atualizarBorda();
    const int64_t agora = esp_timer_get_time();

    if (borda > 0) {            // acabou de pressionar: comeca a cronometrar
        t_press_inicio_us_ = agora;
        longo_disparado_   = false;
    } else if (borda < 0) {     // soltou
        // Soltou antes do limiar de "segurar" => foi um TOQUE CURTO.
        if (!longo_disparado_) return Clique::Curto;
    }

    // Ainda pressionado e o tempo segurado cruzou o limiar: emite LONGO 1 vez
    // (nao espera soltar, para o operador sentir o momento em que "pegou").
    if (estavel_pressionado_ && !longo_disparado_ &&
        (agora - t_press_inicio_us_) >= (int64_t)ms_longo * 1000) {
        longo_disparado_ = true;
        return Clique::Longo;
    }
    return Clique::Nenhum;
}
