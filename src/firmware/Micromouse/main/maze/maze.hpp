#pragma once

#include <cstdint>

// =============================================================================
//  Labirinto :: navegacao de micromouse com flood fill OTIMISTA e estrategia
//               PATH-FOCUSED (ver maze/prompt_micromouse_pathfocused.md).
//
//  Principio otimista (ver temParede/floodFill): celulas nunca sensoriadas sao
//  tratadas como ABERTAS ate prova em contrario. Uma parede so "existe" no mapa
//  depois de lida por sensor (bit setado). Isso faz o robo se arriscar pelo
//  desconhecido rumo ao centro. A informacao e monotonica: paredes so aparecem,
//  nunca somem -> uma descoberta nunca encurta o caminho, so o mantem ou alonga.
//  E isso que garante a terminacao do laco de exploracao.
//
//  STUBS DE HARDWARE: o movimento fisico (girar/avancar) e injetado via
//  InterfaceRobo (configurarRobo). A leitura dos sensores chega em passo() como
//  LeituraSensores (paredes RELATIVAS ao heading atual). Ligue ambos ao firmware.
// =============================================================================
class Labirinto {
public:
    static constexpr uint8_t  kMaxSize = 16;
    static constexpr uint16_t kDistanciaInfinita = 0xFFFF;
    static constexpr uint16_t kMaxCaminho = static_cast<uint16_t>(kMaxSize) * kMaxSize;

    enum class Tamanho : uint8_t {
        k4x4 = 4,
        k8x8 = 8,
        k16x16 = 16,
    };

    // Direcoes absolutas. A ordem (N=0, L=1, S=2, O=3) casa com o enum Heading
    // do firmware, permitindo converter Direcao <-> Heading por cast direto.
    enum class Direcao : uint8_t {
        Norte = 0,
        Leste = 1,
        Sul   = 2,
        Oeste = 3,
        Nenhuma = 255,
    };

    // As quatro fases da maquina de estados (ver passo()).
    enum class Fase : uint8_t {
        ExplorarAteObjetivo,  // desce o gradiente rumo ao centro
        RefinarCaminho,       // verifica as celulas incertas sobre o caminho otimo
        RetornarAoInicio,     // volta a largada pelo gradiente, sem explorar
        FastRun,              // corrida rapida sobre o mapa fechado
        Concluido,
    };

    // O que aconteceu no ultimo passo (para a telemetria reagir no firmware).
    enum class Resultado : uint8_t {
        EmProgresso,       // moveu-se uma celula normalmente
        AlcancouObjetivo,  // acabou de chegar ao centro (fim de EXPLORAR)
        CaminhoFechado,    // caminho otimo totalmente conhecido (fim de REFINAR)
        RetornouAoInicio,  // de volta a largada (fim de RETORNAR)
        FastRunCompleto,   // chegou ao centro na corrida rapida
        Bloqueado,         // nao ha movimento possivel
    };

    // Bits de parede CONHECIDA. Mantidos compativeis com a telemetria/web:
    // Norte=1, Sul=2, Leste=4, Oeste=8.
    enum Parede : uint8_t {
        ParedeNorte = 1 << 0,
        ParedeSul   = 1 << 1,
        ParedeLeste = 1 << 2,
        ParedeOeste = 1 << 3,
    };

    struct Posicao {
        uint8_t x;
        uint8_t y;
        bool operator==(const Posicao &o) const { return x == o.x && y == o.y; }
        bool operator!=(const Posicao &o) const { return !(*this == o); }
    };

    // Compatibilidade com os modulos de telemetria/envio_dados.
    using Coordenada = Posicao;

    // Leitura dos 3 ToFs, RELATIVA ao heading atual do robo.
    struct LeituraSensores {
        bool parede_frente;
        bool parede_esquerda;
        bool parede_direita;
    };

    // Stubs de movimento ligados ao firmware. virarPara recebe a direcao
    // ABSOLUTA de destino (deve girar pelo menor angulo); avancar move 1 celula.
    struct InterfaceRobo {
        void (*virarPara)(Direcao destino) = nullptr;
        void (*avancar)() = nullptr;
    };

    static constexpr Posicao kInvalida = {0xFF, 0xFF};

    Labirinto();

    void configurar(Tamanho tamanho);
    uint8_t tamanho() const;

    void configurarRobo(const InterfaceRobo &robo);

    // Define largada/objetivo e zera todo o estado de navegacao.
    void iniciar(Posicao inicio, Posicao objetivo);

    // Um passo da maquina de estados; chamar repetidamente ate FastRunCompleto.
    Resultado passo(const LeituraSensores &s);

    Fase fase() const;
    Posicao posicao() const;
    Direcao heading() const;
    Posicao objetivo() const;
    Posicao inicio() const;

    // Ultima celula sensoriada neste/no ultimo passo (para telemetria tipo 1).
    bool sensoriou() const;
    Posicao posicaoSensoriada() const;

    // Acessos de leitura ao mapa.
    bool dentroDosLimites(Posicao p) const;
    bool visitada(Posicao p) const;
    uint8_t paredes(Posicao p) const;     // somente os bits N/S/L/O
    uint16_t distancia(Posicao p) const;  // do ultimo floodFill

    // Conveniencia: refloda o objetivo e coleta a rota otima inicio->centro.
    uint16_t rotaOtima(Posicao *buffer, uint16_t capacidade);

    // --- Nucleo do algoritmo (exposto conforme o enunciado) ---------------

    // BFS reversa a partir de `destino`, respeitando SO paredes conhecidas.
    void floodFill(Posicao destino);

    // Vizinho aberto de menor distancia a partir da posicao atual (desempate:
    // seguir reto em vez de girar).
    Direcao proximaDirecaoFlood() const;

    // Segue o gradiente de `dist_` (assume floodFill(objetivo)) de inicio ao
    // objetivo, preenchendo `caminho`.
    void coletarCaminhoOtimo(Posicao caminho[], uint8_t &n) const;

    // true se TODAS as celulas do caminho otimo atual ja foram visitadas
    // (assume que floodFill(objetivo) foi rodado antes).
    bool caminhoTotalmenteConhecido() const;

    // Celula incerta (nao visitada) mais proxima do robo que esta SOBRE o
    // caminho otimo atual. ATENCAO A ORDEM DOS FLOODS: coleta o caminho com o
    // dist_ vigente (floodFill(objetivo)) ANTES de refloodar de pos_.
    Posicao celulaIncertaMaisProxima();

    void atualizarParedes(Posicao p, const LeituraSensores &s);
    bool temParede(Posicao p, Direcao d) const;
    Posicao vizinho(Posicao p, Direcao d) const;

    // Vizinho aberto que mais reduz a distancia a partir de `p` (desempate reto).
    Direcao direcaoDeMenorDist(Posicao p) const;

private:
    static constexpr uint8_t kBitVisitada = 1 << 4;

    uint8_t  n_;                              // lado efetivo do labirinto
    uint8_t  mapa_[kMaxSize][kMaxSize];       // bits de parede + bit de visitada
    uint16_t dist_[kMaxSize][kMaxSize];       // distancias do ultimo floodFill

    Posicao inicio_;
    Posicao objetivo_;
    Posicao pos_;
    Direcao heading_;
    Fase    fase_;
    Posicao alvoExploracao_;                  // alvo temporario da fase REFINAR

    bool    sensoriou_;
    Posicao posSensoriada_;

    InterfaceRobo robo_;

    void resetarMapa();
    void definirParede(Posicao p, Direcao d);
    void marcarVisitada(Posicao p);
    void mover(Direcao d);

    static Direcao oposta(Direcao d);
    static Direcao girarCW(Direcao d);
    static Direcao girarCCW(Direcao d);
    static uint8_t bitDe(Direcao d);
};
