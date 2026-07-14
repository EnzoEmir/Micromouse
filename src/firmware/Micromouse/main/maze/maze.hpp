#pragma once

#include <cstdint>

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

    enum class Direcao : uint8_t {
        Norte = 0,
        Leste = 1,
        Sul   = 2,
        Oeste = 3,
        Nenhuma = 255,
    };

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

    struct LeituraSensores {
        bool parede_frente;
        bool parede_esquerda;
        bool parede_direita;
    };

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

    // Prepara a corrida rapida MANTENDO o mapa aprendido: reposiciona o robo
    // na largada (pos_=inicio_, heading_=Norte), entra na fase FastRun e
    // refloda o objetivo. Use quando o robo for fisicamente recolocado na
    // largada apos o mapeamento (iniciar() nao serve: apaga o mapa).
    void prepararFastRun();

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

    // Bloco objetivo 2x2 no centro do labirinto, derivado do tamanho n_ (para
    // 4x4 -> {1,2}x{1,2}; 8x8 -> {3,4}x{3,4}). Preenche `out` com as celulas
    // dentro dos limites e devolve quantas em `n`. O mapeamento so termina apos
    // visitar TODAS as alcancaveis (confirma que o 2x2 esta aberto e realmente
    // foi alcancado); a corrida rapida termina ao entrar em QUALQUER uma delas.
    void celulasCentro(Posicao out[4], uint8_t &n) const;
    bool ehCelulaCentro(Posicao p) const;

    // BFS reversa a partir de `destino`, respeitando SO paredes conhecidas.
    void floodFill(Posicao destino);

    Direcao proximaDirecaoFlood() const;

    void coletarCaminhoOtimo(Posicao caminho[], uint8_t &n) const;

    bool caminhoTotalmenteConhecido() const;

    Posicao celulaIncertaMaisProxima();

    void atualizarParedes(Posicao p, const LeituraSensores &s);
    bool temParede(Posicao p, Direcao d) const;
    Posicao vizinho(Posicao p, Direcao d) const;

    Direcao direcaoDeMenorDist(Posicao p) const;

    // Corrida rapida: comprimento do trecho RETO a partir de pos_ seguindo o
    // gradiente ate o centro. Preenche `dir_saida` (direcao do trecho) e
    // `termina_em_parede` (o trecho acaba de frente para uma parede -> curva
    // forcada; batente confiavel para o ToF frontal). Retorna o numero de tiles
    // retos (0 se ja no centro / sem descida). Recalcula o flood para o centro.
    uint8_t retaFastRun(Direcao &dir_saida, bool &termina_em_parede);

    // Corrida rapida: avanca o MODELO (pos_/heading_) n celulas na direcao d,
    // sem acionar o robo. Usado quando o firmware ja dirigiu o trecho reto
    // inteiro de uma vez (dash sem parar ate a parede).
    void avancarModeloFastRun(Direcao d, uint8_t n);

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

    // Flood-fill multi-fonte a partir de todas as celulas do bloco central
    // (todas com distancia 0): gera o gradiente que leva ao tile do centro mais
    // proximo. Usado na corrida rapida.
    void floodFillCentro();
    // Celula do bloco central ainda NAO visitada e alcancavel mais proxima de
    // pos_ (kInvalida se todas visitadas ou nenhuma alcancavel).
    Posicao centroNaoVisitadoMaisProximo();
    // true se ao menos uma celula do bloco central ja foi visitada.
    bool algumCentroVisitado() const;

    static Direcao oposta(Direcao d);
    static Direcao girarCW(Direcao d);
    static Direcao girarCCW(Direcao d);
    static uint8_t bitDe(Direcao d);
};
