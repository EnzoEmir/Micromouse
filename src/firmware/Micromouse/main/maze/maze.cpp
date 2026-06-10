#include "maze/maze.hpp"

Labirinto::Labirinto()
    : n_(static_cast<uint8_t>(Tamanho::k16x16)),
      inicio_{0, 0},
      objetivo_{0, 0},
      pos_{0, 0},
      heading_(Direcao::Norte),
      fase_(Fase::ExplorarAteObjetivo),
      alvoExploracao_(kInvalida),
      sensoriou_(false),
      posSensoriada_{0, 0},
      robo_{} {
    resetarMapa();
}

void Labirinto::configurar(Tamanho tamanho) {
    n_ = static_cast<uint8_t>(tamanho);
    resetarMapa();
}

uint8_t Labirinto::tamanho() const {
    return n_;
}

void Labirinto::configurarRobo(const InterfaceRobo &robo) {
    robo_ = robo;
}

void Labirinto::iniciar(Posicao inicio, Posicao objetivo) {
    resetarMapa();
    inicio_ = inicio;
    objetivo_ = objetivo;
    pos_ = inicio;
    heading_ = Direcao::Norte;
    fase_ = Fase::ExplorarAteObjetivo;
    alvoExploracao_ = kInvalida;
    sensoriou_ = false;
    posSensoriada_ = inicio;
}

void Labirinto::resetarMapa() {
    for (uint8_t y = 0; y < kMaxSize; ++y) {
        for (uint8_t x = 0; x < kMaxSize; ++x) {
            mapa_[y][x] = 0;
            dist_[y][x] = kDistanciaInfinita;
        }
    }
}

Labirinto::Direcao Labirinto::oposta(Direcao d) {
    switch (d) {
        case Direcao::Norte: return Direcao::Sul;
        case Direcao::Sul:   return Direcao::Norte;
        case Direcao::Leste: return Direcao::Oeste;
        case Direcao::Oeste: return Direcao::Leste;
        default:             return Direcao::Nenhuma;
    }
}

Labirinto::Direcao Labirinto::girarCW(Direcao d) {
    return static_cast<Direcao>((static_cast<uint8_t>(d) + 1) & 0x03);
}

Labirinto::Direcao Labirinto::girarCCW(Direcao d) {
    return static_cast<Direcao>((static_cast<uint8_t>(d) + 3) & 0x03);
}

uint8_t Labirinto::bitDe(Direcao d) {
    switch (d) {
        case Direcao::Norte: return ParedeNorte;
        case Direcao::Leste: return ParedeLeste;
        case Direcao::Sul:   return ParedeSul;
        case Direcao::Oeste: return ParedeOeste;
        default:             return 0;
    }
}

Labirinto::Posicao Labirinto::vizinho(Posicao p, Direcao d) const {
    switch (d) {
        case Direcao::Norte: return {p.x, static_cast<uint8_t>(p.y + 1)};
        case Direcao::Sul:   return {p.x, static_cast<uint8_t>(p.y - 1)};
        case Direcao::Leste: return {static_cast<uint8_t>(p.x + 1), p.y};
        case Direcao::Oeste: return {static_cast<uint8_t>(p.x - 1), p.y};
        default:             return p;
    }
}

bool Labirinto::dentroDosLimites(Posicao p) const {
    return p.x < n_ && p.y < n_;
}

bool Labirinto::visitada(Posicao p) const {
    if (!dentroDosLimites(p)) return false;
    return (mapa_[p.y][p.x] & kBitVisitada) != 0;
}

uint8_t Labirinto::paredes(Posicao p) const {
    if (!dentroDosLimites(p)) return 0;
    return mapa_[p.y][p.x] & 0x0F;
}

uint16_t Labirinto::distancia(Posicao p) const {
    if (!dentroDosLimites(p)) return kDistanciaInfinita;
    return dist_[p.y][p.x];
}

void Labirinto::marcarVisitada(Posicao p) {
    if (dentroDosLimites(p)) mapa_[p.y][p.x] |= kBitVisitada;
}

void Labirinto::definirParede(Posicao p, Direcao d) {
    if (dentroDosLimites(p)) mapa_[p.y][p.x] |= bitDe(d);
    const Posicao v = vizinho(p, d);
    if (dentroDosLimites(v)) mapa_[v.y][v.x] |= bitDe(oposta(d));
}

bool Labirinto::temParede(Posicao p, Direcao d) const {
    if (!dentroDosLimites(p)) return true;
    if (!dentroDosLimites(vizinho(p, d))) return true;
    return (mapa_[p.y][p.x] & bitDe(d)) != 0;
}

void Labirinto::atualizarParedes(Posicao p, const LeituraSensores &s) {
    if (!dentroDosLimites(p)) return;
    if (s.parede_frente)   definirParede(p, heading_);
    if (s.parede_esquerda) definirParede(p, girarCCW(heading_));
    if (s.parede_direita)  definirParede(p, girarCW(heading_));
    marcarVisitada(p);
}

void Labirinto::floodFill(Posicao destino) {
    for (uint8_t y = 0; y < kMaxSize; ++y) {
        for (uint8_t x = 0; x < kMaxSize; ++x) {
            dist_[y][x] = kDistanciaInfinita;
        }
    }
    if (!dentroDosLimites(destino)) return;

    Posicao fila[kMaxCaminho];
    uint16_t head = 0;
    uint16_t tail = 0;

    dist_[destino.y][destino.x] = 0;
    fila[tail++] = destino;

    const Direcao dirs[4] = {Direcao::Norte, Direcao::Leste, Direcao::Sul, Direcao::Oeste};

    while (head < tail) {
        const Posicao a = fila[head++];
        const uint16_t nd = dist_[a.y][a.x] + 1;
        for (uint8_t i = 0; i < 4; ++i) {
            if (temParede(a, dirs[i])) continue;
            const Posicao v = vizinho(a, dirs[i]);
            if (dist_[v.y][v.x] > nd) {
                dist_[v.y][v.x] = nd;
                fila[tail++] = v;
            }
        }
    }
}

Labirinto::Direcao Labirinto::direcaoDeMenorDist(Posicao p) const {
    if (!dentroDosLimites(p)) return Direcao::Nenhuma;

    const uint16_t atual = dist_[p.y][p.x];
    const Direcao dirs[4] = {Direcao::Norte, Direcao::Leste, Direcao::Sul, Direcao::Oeste};

    uint16_t minD = atual;
    for (uint8_t i = 0; i < 4; ++i) {
        if (temParede(p, dirs[i])) continue;
        const Posicao v = vizinho(p, dirs[i]);
        if (dist_[v.y][v.x] < minD) minD = dist_[v.y][v.x];
    }
    if (minD >= atual) return Direcao::Nenhuma;

    Direcao escolha = Direcao::Nenhuma;
    for (uint8_t i = 0; i < 4; ++i) {
        if (temParede(p, dirs[i])) continue;
        const Posicao v = vizinho(p, dirs[i]);
        if (dist_[v.y][v.x] != minD) continue;
        if (dirs[i] == heading_) return dirs[i];
        if (escolha == Direcao::Nenhuma) escolha = dirs[i];
    }
    return escolha;
}

Labirinto::Direcao Labirinto::proximaDirecaoFlood() const {
    return direcaoDeMenorDist(pos_);
}

void Labirinto::coletarCaminhoOtimo(Posicao caminho[], uint8_t &n) const {
    n = 0;
    Posicao a = inicio_;
    while (true) {
        caminho[n++] = a;
        if (a == objetivo_) return;
        if (n >= kMaxSize * kMaxSize - 1) return;
        const Direcao d = direcaoDeMenorDist(a);
        if (d == Direcao::Nenhuma) return;
        a = vizinho(a, d);
    }
}

bool Labirinto::caminhoTotalmenteConhecido() const {
    Posicao caminho[kMaxSize * kMaxSize];
    uint8_t n = 0;
    coletarCaminhoOtimo(caminho, n);

    if (n == 0) return false;
    if (caminho[n - 1] != objetivo_) return false;
    for (uint8_t i = 0; i < n; ++i) {
        if (!visitada(caminho[i])) return false;
    }
    return true;
}

Labirinto::Posicao Labirinto::celulaIncertaMaisProxima() {
    Posicao caminho[kMaxSize * kMaxSize];
    uint8_t n = 0;
    coletarCaminhoOtimo(caminho, n);

    floodFill(pos_);

    Posicao melhor = kInvalida;
    uint16_t melhorD = kDistanciaInfinita;
    for (uint8_t i = 0; i < n; ++i) {
        const Posicao c = caminho[i];
        if (visitada(c)) continue;
        const uint16_t d = dist_[c.y][c.x];
        if (d < melhorD) {
            melhorD = d;
            melhor = c;
        }
    }
    return melhor;
}

uint16_t Labirinto::rotaOtima(Posicao *buffer, uint16_t capacidade) {
    floodFill(objetivo_);
    Posicao tmp[kMaxSize * kMaxSize];
    uint8_t n = 0;
    coletarCaminhoOtimo(tmp, n);
    uint16_t out = 0;
    for (uint8_t i = 0; i < n && out < capacidade; ++i) buffer[out++] = tmp[i];
    return out;
}

void Labirinto::mover(Direcao d) {
    if (robo_.virarPara) robo_.virarPara(d);
    if (robo_.avancar)   robo_.avancar();
    heading_ = d;
    pos_ = vizinho(pos_, d);
}

Labirinto::Resultado Labirinto::passo(const LeituraSensores &s) {
    sensoriou_ = false;

    if (fase_ == Fase::ExplorarAteObjetivo || fase_ == Fase::RefinarCaminho) {
        atualizarParedes(pos_, s);
        sensoriou_ = true;
        posSensoriada_ = pos_;
    }

    switch (fase_) {
        case Fase::ExplorarAteObjetivo: {
            floodFill(objetivo_);
            if (pos_ == objetivo_) {
                fase_ = Fase::RefinarCaminho;
                alvoExploracao_ = kInvalida;
                return Resultado::AlcancouObjetivo;
            }
            const Direcao d = proximaDirecaoFlood();
            if (d == Direcao::Nenhuma) return Resultado::Bloqueado;
            mover(d);
            return Resultado::EmProgresso;
        }

        case Fase::RefinarCaminho: {
            floodFill(objetivo_);
            if (caminhoTotalmenteConhecido()) {
                fase_ = Fase::RetornarAoInicio;
                alvoExploracao_ = kInvalida;
                return Resultado::CaminhoFechado;
            }

            if (alvoExploracao_ == kInvalida || pos_ == alvoExploracao_) {
                alvoExploracao_ = celulaIncertaMaisProxima();
            }
            if (alvoExploracao_ == kInvalida) {
                fase_ = Fase::RetornarAoInicio;
                return Resultado::CaminhoFechado;
            }

            floodFill(alvoExploracao_);
            const Direcao d = direcaoDeMenorDist(pos_);
            if (d == Direcao::Nenhuma) {
                alvoExploracao_ = kInvalida;
                return Resultado::Bloqueado;
            }
            mover(d);
            return Resultado::EmProgresso;
        }

        case Fase::RetornarAoInicio: {
            floodFill(inicio_);
            if (pos_ == inicio_) {
                floodFill(objetivo_);
                fase_ = Fase::FastRun;
                return Resultado::RetornouAoInicio;
            }
            const Direcao d = direcaoDeMenorDist(pos_);
            if (d == Direcao::Nenhuma) return Resultado::Bloqueado;
            mover(d);
            return Resultado::EmProgresso;
        }

        case Fase::FastRun: {
            if (pos_ == objetivo_) {
                fase_ = Fase::Concluido;
                return Resultado::FastRunCompleto;
            }
            const Direcao d = direcaoDeMenorDist(pos_);
            if (d == Direcao::Nenhuma) return Resultado::Bloqueado;
            mover(d);
            return Resultado::EmProgresso;
        }

        case Fase::Concluido:
        default:
            return Resultado::FastRunCompleto;
    }
}

Labirinto::Fase Labirinto::fase() const { return fase_; }
Labirinto::Posicao Labirinto::posicao() const { return pos_; }
Labirinto::Direcao Labirinto::heading() const { return heading_; }
Labirinto::Posicao Labirinto::objetivo() const { return objetivo_; }
Labirinto::Posicao Labirinto::inicio() const { return inicio_; }
bool Labirinto::sensoriou() const { return sensoriou_; }
Labirinto::Posicao Labirinto::posicaoSensoriada() const { return posSensoriada_; }
