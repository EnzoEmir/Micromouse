#pragma once

#include <array>
#include <cstdint>

class Labirinto {
public:
    static constexpr uint8_t kMaxSize = 16;

    enum class Tamanho : uint8_t {
        k4x4 = 4,
        k8x8 = 8,
        k16x16 = 16,
    };

    enum class StatusCelula : uint8_t {
        Desconhecida = 0,
        Livre,
        Visitada,
    };

    struct Celula {
        uint8_t walls;     // Bitmask de paredes (N=1, S=2, E=4, W=8)
        StatusCelula status;
    };

    struct Coordenada {
        uint8_t x;
        uint8_t y;
    };

    enum Parede : uint8_t {
        ParedeNorte = 1 << 0,
        ParedeSul   = 1 << 1,
        ParedeLeste = 1 << 2,
        ParedeOeste = 1 << 3,
    };

    Labirinto();

    void configurar(Tamanho tamanho);
    uint8_t tamanho() const;

    bool dentroDosLimites(uint8_t x, uint8_t y) const;

    const Celula &celula(uint8_t x, uint8_t y) const;
    Celula &celula(uint8_t x, uint8_t y);

    void resetar();

    void atualizarCelula(uint8_t x, uint8_t y, uint8_t paredes);

    bool encontrarProximaFronteira(Coordenada *saida) const;

private:
    uint8_t tamanho_;
    std::array<std::array<Celula, kMaxSize>, kMaxSize> grade_;

    void definirParede(uint8_t x, uint8_t y, Parede parede);
    bool temParede(uint8_t x, uint8_t y, Parede parede) const;
};
