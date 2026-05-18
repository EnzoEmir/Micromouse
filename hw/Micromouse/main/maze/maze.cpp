#include "maze/maze.hpp"

Labirinto::Labirinto() : tamanho_(static_cast<uint8_t>(Tamanho::k16x16)) {
    resetar();
}

void Labirinto::configurar(Tamanho tamanho) {
    tamanho_ = static_cast<uint8_t>(tamanho);
    resetar();
}

uint8_t Labirinto::tamanho() const {
    return tamanho_;
}

bool Labirinto::dentroDosLimites(uint8_t x, uint8_t y) const {
    return x < tamanho_ && y < tamanho_;
}

const Labirinto::Celula &Labirinto::celula(uint8_t x, uint8_t y) const {
    return grade_[y][x];
}

Labirinto::Celula &Labirinto::celula(uint8_t x, uint8_t y) {
    return grade_[y][x];
}

void Labirinto::resetar() {
    for (uint8_t y = 0; y < kMaxSize; ++y) {
        for (uint8_t x = 0; x < kMaxSize; ++x) {
            grade_[y][x].walls = 0;
            grade_[y][x].status = StatusCelula::Desconhecida;
        }
    }
}

void Labirinto::definirParede(uint8_t x, uint8_t y, Parede parede) {
    if (!dentroDosLimites(x, y)) return;
    grade_[y][x].walls |= static_cast<uint8_t>(parede);
}

bool Labirinto::temParede(uint8_t x, uint8_t y, Parede parede) const {
    if (!dentroDosLimites(x, y)) return true;
    return (grade_[y][x].walls & static_cast<uint8_t>(parede)) != 0;
}

void Labirinto::atualizarCelula(uint8_t x, uint8_t y, uint8_t paredes) {
    if (!dentroDosLimites(x, y)) return;

    Celula &atual = grade_[y][x];
    atual.walls = paredes;
    atual.status = StatusCelula::Visitada;

    // Norte
    if (paredes & ParedeNorte) {
        definirParede(x, y + 1, ParedeSul);
    } else if (dentroDosLimites(x, y + 1) && grade_[y + 1][x].status == StatusCelula::Desconhecida) {
        grade_[y + 1][x].status = StatusCelula::Livre;
    }

    // Sul
    if (paredes & ParedeSul) {
        if (y > 0) definirParede(x, y - 1, ParedeNorte);
    } else if (y > 0 && grade_[y - 1][x].status == StatusCelula::Desconhecida) {
        grade_[y - 1][x].status = StatusCelula::Livre;
    }

    // Leste
    if (paredes & ParedeLeste) {
        definirParede(x + 1, y, ParedeOeste);
    } else if (dentroDosLimites(x + 1, y) && grade_[y][x + 1].status == StatusCelula::Desconhecida) {
        grade_[y][x + 1].status = StatusCelula::Livre;
    }

    // Oeste
    if (paredes & ParedeOeste) {
        if (x > 0) definirParede(x - 1, y, ParedeLeste);
    } else if (x > 0 && grade_[y][x - 1].status == StatusCelula::Desconhecida) {
        grade_[y][x - 1].status = StatusCelula::Livre;
    }
}

bool Labirinto::encontrarProximaFronteira(Coordenada *saida) const {
    if (!saida) return false;

    for (uint8_t y = 0; y < tamanho_; ++y) {
        for (uint8_t x = 0; x < tamanho_; ++x) {
            const Celula &c = grade_[y][x];
            if (c.status == StatusCelula::Desconhecida) continue;

            const bool norte_desconhecido = dentroDosLimites(x, y + 1) &&
                !temParede(x, y, ParedeNorte) &&
                grade_[y + 1][x].status == StatusCelula::Desconhecida;
            const bool sul_desconhecido = (y > 0) &&
                !temParede(x, y, ParedeSul) &&
                grade_[y - 1][x].status == StatusCelula::Desconhecida;
            const bool leste_desconhecido = dentroDosLimites(x + 1, y) &&
                !temParede(x, y, ParedeLeste) &&
                grade_[y][x + 1].status == StatusCelula::Desconhecida;
            const bool oeste_desconhecido = (x > 0) &&
                !temParede(x, y, ParedeOeste) &&
                grade_[y][x - 1].status == StatusCelula::Desconhecida;

            if (norte_desconhecido || sul_desconhecido || leste_desconhecido || oeste_desconhecido) {
                saida->x = x;
                saida->y = y;
                return true;
            }
        }
    }

    return false;
}
