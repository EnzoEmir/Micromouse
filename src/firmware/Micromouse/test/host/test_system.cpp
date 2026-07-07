// Testes de sistema do firmware
//
//     Battery (SOC real)  ->  Labirinto (missao completa) -> Telemetria (orquestracao + gating de Wi-Fi) -> envio_dados (serializa o JSON real) -> [ borda da rede: HTTP mock ]  ->  Backend simulado
//
// Apenas as bordas de hardware/rede sao mockadas (Wi-Fi, HTTP, relogio virtual e o INA226/I2C). O "backend simulado" abaixo faz o parse do JSON que realmente
// cruzou a rede em cada evento, exatamente como o servidor web faria, e os testes asseguram que uma missao do robo produz um fluxo de telemetria coerente.
#include "framework.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "battery/battery.hpp"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "ina226.hpp"
#include "maze/maze.hpp"
#include "telemetria.hpp"
#include "wifi.hpp"

extern "C" {
#include "cJSON.h"
}

using D = Labirinto::Direcao;
using P = Labirinto::Posicao;
using R = Labirinto::Resultado;
using F = Labirinto::Fase;

static const char* URL = "http://backend.test/telemetria";

//  Labirinto de referencia (ground truth) que gera as leituras de sensores que o robo obteria em cada posicao/orientacao.
struct GroundTruth {
    uint8_t n;
    uint8_t walls[16][16];  // N=1, S=2, L=4, O=8

    void clear(uint8_t size) {
        n = size;
        for (auto& row : walls)
            for (auto& c : row) c = 0;
    }
    static uint8_t bitDe(D d) {
        switch (d) {
            case D::Norte: return Labirinto::ParedeNorte;
            case D::Sul:   return Labirinto::ParedeSul;
            case D::Leste: return Labirinto::ParedeLeste;
            case D::Oeste: return Labirinto::ParedeOeste;
            default:       return 0;
        }
    }
    static D oposta(D d) {
        switch (d) {
            case D::Norte: return D::Sul;
            case D::Sul:   return D::Norte;
            case D::Leste: return D::Oeste;
            case D::Oeste: return D::Leste;
            default:       return D::Nenhuma;
        }
    }
    static D cw(D d) { return static_cast<D>((static_cast<uint8_t>(d) + 1) & 3); }
    static D ccw(D d) { return static_cast<D>((static_cast<uint8_t>(d) + 3) & 3); }
    static P vizinho(P p, D d) {
        switch (d) {
            case D::Norte: return {p.x, (uint8_t)(p.y + 1)};
            case D::Sul:   return {p.x, (uint8_t)(p.y - 1)};
            case D::Leste: return {(uint8_t)(p.x + 1), p.y};
            case D::Oeste: return {(uint8_t)(p.x - 1), p.y};
            default:       return p;
        }
    }
    bool dentro(P p) const { return p.x < n && p.y < n; }
    void addWall(P p, D d) {
        if (dentro(p)) walls[p.y][p.x] |= bitDe(d);
        P v = vizinho(p, d);
        if (dentro(v)) walls[v.y][v.x] |= bitDe(oposta(d));
    }
    bool temParede(P p, D d) const {
        if (!dentro(p)) return true;
        if (!dentro(vizinho(p, d))) return true;
        return (walls[p.y][p.x] & bitDe(d)) != 0;
    }
    Labirinto::LeituraSensores ler(P p, D heading) const {
        Labirinto::LeituraSensores s;
        s.parede_frente   = temParede(p, heading);
        s.parede_esquerda = temParede(p, ccw(heading));
        s.parede_direita  = temParede(p, cw(heading));
        return s;
    }
};

//  Backend simulado: faz o parse de cada pacote JSON que cruzou a borda HTTP.
struct Pacote {
    int       tipo      = -1;
    long long ts        = 0;
    int       x         = -1, y = -1, w = -1;
    int       dimensao  = -1, bateria = -1;
    int       sucesso   = -1;  // -1 ausente, 0/1 presente
    double    v_med     = 0.0, temp_c = 0.0;
    int       rota_size = -1;
};

static int numInt(cJSON* o, const char* k, int def) {
    cJSON* it = cJSON_GetObjectItem(o, k);
    return (it && cJSON_IsNumber(it)) ? (int)it->valuedouble : def;
}
static long long numLL(cJSON* o, const char* k, long long def) {
    cJSON* it = cJSON_GetObjectItem(o, k);
    return (it && cJSON_IsNumber(it)) ? (long long)it->valuedouble : def;
}
static double numDbl(cJSON* o, const char* k, double def) {
    cJSON* it = cJSON_GetObjectItem(o, k);
    return (it && cJSON_IsNumber(it)) ? it->valuedouble : def;
}

struct Backend {
    std::vector<Pacote> recebidos;

    void reset() {
        recebidos.clear();
        mock_http_reset();
    }

    // Captura o ultimo corpo se um novo POST ocorreu desde `post_count_antes`.
    bool capturarSe(int post_count_antes) {
        if (mock_http_post_count() <= post_count_antes) return false;
        cJSON* j = cJSON_Parse(mock_http_last_body());
        if (!j) return false;
        Pacote p;
        p.tipo     = numInt(j, "tipo", -1);
        p.ts       = numLL(j, "timestamp_ms", -1);
        p.x        = numInt(j, "x", -1);
        p.y        = numInt(j, "y", -1);
        p.w        = numInt(j, "w", -1);
        p.dimensao = numInt(j, "dimensao", -1);
        p.bateria  = numInt(j, "bateria", -1);
        cJSON* suc = cJSON_GetObjectItem(j, "sucesso");
        if (suc && cJSON_IsBool(suc)) p.sucesso = cJSON_IsTrue(suc) ? 1 : 0;
        p.v_med  = numDbl(j, "v_med", 0.0);
        p.temp_c = numDbl(j, "temp_c", 0.0);
        cJSON* arr = cJSON_GetObjectItem(j, "rota");
        if (arr && cJSON_IsArray(arr)) p.rota_size = cJSON_GetArraySize(arr);
        recebidos.push_back(p);
        cJSON_Delete(j);
        return true;
    }

    int contar(int tipo) const {
        int c = 0;
        for (const auto& p : recebidos)
            if (p.tipo == tipo) c++;
        return c;
    }
    const Pacote* primeiro() const { return recebidos.empty() ? nullptr : &recebidos.front(); }
    const Pacote* ultimo() const { return recebidos.empty() ? nullptr : &recebidos.back(); }
};

//  Bancada: monta um sistema completo num estado conhecido.
struct Bancada {
    Labirinto lab;
    GroundTruth gt;
    Battery bat;
    Backend be;

    // volts/amps definem o estado eletrico inicial; amps < 0 descarrega.
    void preparar(Labirinto::Tamanho tam, float volts, float amps) {
        mock_timer_reset();
        mock_wifi_reset();
        espp::mock_ina226_set(volts, amps, volts * (amps < 0 ? -amps : amps));
        bat.init();  // semeia SOC a partir da tensao, com last_update em t=0
        be.reset();
        lab.configurar(tam);
        gt.clear(static_cast<uint8_t>(tam));
    }

    int socInt() { return (int)std::lround(bat.getSOC()); }
};

struct ResultadoMissao {
    R    resultado    = R::EmProgresso;
    int  passos       = 0;
    int  movimentos   = 0;  // pacotes tipo 1 capturados
    bool enviou_rota  = false;
    bool enviou_fim   = false;
};

// Roda o ciclo completo (Explorar -> Refinar -> Retornar -> FastRun) emitindo a
// telemetria real a cada evento, com o relogio e a bateria avancando por passo.
static ResultadoMissao executarMissao(Bancada& bk, Telemetria& tel,
                                      int64_t dt_us, int max_steps) {
    ResultadoMissao rm;
    R res = R::EmProgresso;
    int i = 0;
    for (; i < max_steps; ++i) {
        auto s = bk.gt.ler(bk.lab.posicao(), bk.lab.heading());
        res = bk.lab.passo(s);

        if (bk.lab.sensoriou()) {
            P p = bk.lab.posicaoSensoriada();
            int pc = mock_http_post_count();
            tel.movimento(p.x, p.y, bk.lab.paredes(p));
            if (bk.be.capturarSe(pc)) rm.movimentos++;
        }

        mock_timer_advance_us(dt_us);  // tempo de percurso da celula
        bk.bat.update();               // integra a descarga (coulomb counting)

        if (res == R::AlcancouObjetivo) {
            P buf[Labirinto::kMaxCaminho];
            uint16_t n = bk.lab.rotaOtima(buf, Labirinto::kMaxCaminho);
            int pc = mock_http_post_count();
            tel.rota_otimizada(buf, n);
            if (bk.be.capturarSe(pc)) rm.enviou_rota = true;
        }

        if (res == R::FastRunCompleto || res == R::Bloqueado) break;
    }
    rm.resultado = res;
    rm.passos = i;

    int pc = mock_http_post_count();
    tel.fim_corrida(res == R::FastRunCompleto, 0.75f, bk.socInt());
    if (bk.be.capturarSe(pc)) rm.enviou_fim = true;
    return rm;
}

//  CASO 1: missao completa em labirinto aberto.
TEST_CASE(sistema_missao_completa_em_labirinto_aberto) {
    Bancada bk;
    bk.preparar(Labirinto::Tamanho::k8x8, /*volts=*/7.5f, /*amps=*/-2.0f);  // ~50% SOC
    bk.lab.iniciar(P{0, 0}, P{4, 4});

    int soc_inicial = bk.socInt();
    CHECK_EQ(soc_inicial, 50);  // 7.5 V -> 50%

    Telemetria tel(URL, /*heartbeat_ms=*/1500);
    tel.inicializar("ssid", "senha", bk.lab, soc_inicial);
    // O pacote de configuracao inicial (tipo 0) ja cruzou a rede.
    bk.be.capturarSe(mock_http_post_count() - 1);

    ResultadoMissao rm = executarMissao(bk, tel, /*dt_us=*/1'000'000LL, /*max=*/8000);

    // A missao terminou com sucesso em uma celula do bloco central do maze.
    CHECK_EQ(rm.resultado, R::FastRunCompleto);
    CHECK_EQ(bk.lab.fase(), F::Concluido);
    CHECK(bk.lab.ehCelulaCentro(bk.lab.posicao()));

    // O backend viu um inicio, varios movimentos, uma rota e um fim.
    const Pacote* prim = bk.be.primeiro();
    REQUIRE(prim != nullptr);
    CHECK_EQ(prim->tipo, 0);             // configuracao inicial primeiro
    CHECK_EQ(prim->dimensao, 8);         // dimensao real do labirinto
    CHECK_EQ(prim->bateria, soc_inicial);
    CHECK_EQ((int)prim->ts, 0);          // relogio zerado na largada

    CHECK(bk.be.contar(1) > 0);          // houve movimentacoes
    CHECK_EQ(rm.movimentos, bk.be.contar(1));
    CHECK(rm.enviou_rota);
    CHECK_EQ(bk.be.contar(2), 1);        // exatamente uma rota otima
    CHECK(rm.enviou_fim);

    const Pacote* ult = bk.be.ultimo();
    REQUIRE(ult != nullptr);
    CHECK_EQ(ult->tipo, 3);              // fim de corrida por ultimo
    CHECK_EQ(ult->sucesso, 1);

    // Os timestamps observados nunca andam para tras.
    long long anterior = -1;
    bool monotonico = true;
    for (const auto& p : bk.be.recebidos) {
        if (p.ts < anterior) monotonico = false;
        anterior = p.ts;
    }
    CHECK(monotonico);

    // A bateria descarregou ao longo da corrida.
    CHECK(ult->bateria < soc_inicial);
    CHECK(ult->bateria >= 0);
}

//  CASO 2: as paredes reportadas pela telemetria sao fieis ao mundo real.
TEST_CASE(sistema_paredes_reportadas_sao_fieis_ao_mapa) {
    Bancada bk;
    bk.preparar(Labirinto::Tamanho::k8x8, 7.4f, -1.5f);
    // Barreira em x=2, y=0..3, com passagem por cima -> objetivo alcancavel.
    for (uint8_t y = 0; y <= 3; ++y) bk.gt.addWall(P{2, y}, D::Leste);
    bk.lab.iniciar(P{0, 0}, P{4, 4});

    Telemetria tel(URL);
    tel.inicializar("s", "p", bk.lab, bk.socInt());
    bk.be.capturarSe(mock_http_post_count() - 1);

    ResultadoMissao rm = executarMissao(bk, tel, 500'000LL, 8000);
    CHECK_EQ(rm.resultado, R::FastRunCompleto);

    const D dirs[4] = {D::Norte, D::Leste, D::Sul, D::Oeste};
    int fantasmas = 0;
    int desalinhados = 0;
    for (const auto& p : bk.be.recebidos) {
        if (p.tipo != 1) continue;
        REQUIRE(p.x >= 0 && p.y >= 0);
        P cel{(uint8_t)p.x, (uint8_t)p.y};
        // Toda parede reportada deve existir no ground truth (sem fantasma).
        for (D d : dirs) {
            if (p.w & GroundTruth::bitDe(d)) {
                if (!bk.gt.temParede(cel, d)) fantasmas++;
            }
        }
        // E o bitmask reportado deve bater com o que o maze gravou.
        if ((p.w & 0x0F) != (bk.lab.paredes(cel) & 0x0F)) {
            // O mapa final pode conter paredes descobertas depois deste pacote,
            // entao so checamos que o reportado e subconjunto do conhecido.
            if ((p.w & bk.lab.paredes(cel)) != p.w) desalinhados++;
        }
    }
    CHECK_EQ(fantasmas, 0);
    CHECK_EQ(desalinhados, 0);
    CHECK(bk.be.contar(1) > 0);
}

//  CASO 3: a queda do Wi-Fi interrompe a cadeia inteira; a reconexao retoma.
TEST_CASE(sistema_queda_de_wifi_interrompe_e_reconexao_retoma) {
    Bancada bk;
    bk.preparar(Labirinto::Tamanho::k8x8, 7.5f, -2.0f);
    bk.lab.iniciar(P{0, 0}, P{4, 4});

    Telemetria tel(URL);
    tel.inicializar("s", "p", bk.lab, bk.socInt());  // conecta e envia config

    // Um movimento com Wi-Fi OK cruza a rede.
    int pc = mock_http_post_count();
    esp_err_t r_ok = tel.movimento(1, 0, 0x02);
    CHECK_EQ(r_ok, ESP_OK);
    CHECK(mock_http_post_count() > pc);

    // Wi-Fi cai: nenhuma chamada deve gerar POST, e todas retornam ESP_FAIL.
    mock_wifi_set_connected(false);
    pc = mock_http_post_count();
    CHECK_EQ(tel.movimento(2, 0, 0x00), ESP_FAIL);
    CHECK_EQ(tel.fim_corrida(true, 1.0f, 50), ESP_FAIL);
    CHECK_EQ(tel.alerta_temperatura(80.0f), ESP_FAIL);
    tel.verificar_heartbeat(50);  // tambem suprimido
    CHECK_EQ(mock_http_post_count(), pc);  // rede silenciosa

    // Reconectou: volta a cruzar a borda.
    mock_wifi_set_connected(true);
    pc = mock_http_post_count();
    CHECK_EQ(tel.movimento(2, 0, 0x00), ESP_OK);
    CHECK(mock_http_post_count() > pc);
}

//  CASO 4: uma falha na borda de rede (HTTP) propaga ate o orquestrador e a recuperacao volta a fluir. Mesmo com Wi-Fi conectado, um perform() que
//  falha precisa ser reportado para cima.
TEST_CASE(sistema_falha_de_rede_propaga_e_recupera) {
    Bancada bk;
    bk.preparar(Labirinto::Tamanho::k8x8, 7.5f, -2.0f);
    bk.lab.iniciar(P{0, 0}, P{4, 4});

    Telemetria tel(URL);
    tel.inicializar("s", "p", bk.lab, bk.socInt());

    // O backend rejeita o POST (perform falha) embora o Wi-Fi esteja conectado.
    mock_http_set_perform_result(ESP_FAIL);
    int pc = mock_http_post_count();
    esp_err_t r = tel.movimento(1, 1, 0x00);
    CHECK_EQ(r, ESP_FAIL);                  // erro propagado ao topo
    CHECK_EQ(mock_http_post_count(), pc + 1);  // tentou enviar (1 POST)

    // Rede normaliza: o proximo envio e bem-sucedido.
    mock_http_set_perform_result(ESP_OK);
    pc = mock_http_post_count();
    r = tel.movimento(1, 2, 0x00);
    CHECK_EQ(r, ESP_OK);
    CHECK_EQ(mock_http_post_count(), pc + 1);
}

//  CASO 5: durante a ociosidade, o heartbeat preenche o silencio carregando o
//  SOC atual da bateria (que ja caiu desde a largada); um evento real reseta a
//  janela de inatividade.
TEST_CASE(sistema_heartbeat_preenche_silencio_com_soc_atual) {
    Bancada bk;
    bk.preparar(Labirinto::Tamanho::k8x8, 7.5f, /*amps=*/-7.2f);  // descarga forte
    bk.lab.iniciar(P{0, 0}, P{4, 4});

    Telemetria tel(URL, /*heartbeat_ms=*/1500);
    tel.inicializar("s", "p", bk.lab, bk.socInt());  // ultimo envio em t=0
    bk.be.capturarSe(mock_http_post_count() - 1);
    int soc_largada = bk.socInt();

    // Passa o tempo e descarrega a bateria: 100 s a 7.2 A -> -10% de SOC.
    mock_timer_advance_us(100'000'000LL);
    bk.bat.update();
    int soc_agora = bk.socInt();
    CHECK(soc_agora < soc_largada);

    // Ocioso alem do timeout -> heartbeat com o SOC atual.
    int pc = mock_http_post_count();
    tel.verificar_heartbeat(soc_agora);
    CHECK(bk.be.capturarSe(pc));
    const Pacote* hb = bk.be.ultimo();
    REQUIRE(hb != nullptr);
    CHECK_EQ(hb->tipo, 4);
    CHECK_EQ(hb->bateria, soc_agora);

    // Um movimento real reseta a janela: logo em seguida nao ha heartbeat.
    int64_t agora = esp_timer_get_time();
    tel.movimento(0, 1, 0);                       // registra envio "agora"
    mock_timer_set_us(agora + 1000 * 1000LL);     // +1 s (< 1.5 s)
    pc = mock_http_post_count();
    tel.verificar_heartbeat(soc_agora);
    CHECK_EQ(mock_http_post_count(), pc);          // suprimido
}

//  CASO 6: um alerta critico de temperatura cruza a cadeia no meio da missao
//  sem perturbar o restante do fluxo, e é serializado com o valor correto.
TEST_CASE(sistema_alerta_temperatura_cruza_a_cadeia) {
    Bancada bk;
    bk.preparar(Labirinto::Tamanho::k4x4, 7.5f, -2.0f);
    bk.lab.iniciar(P{0, 0}, P{2, 2});

    Telemetria tel(URL);
    tel.inicializar("s", "p", bk.lab, bk.socInt());
    bk.be.capturarSe(mock_http_post_count() - 1);

    // Alguns passos de exploracao, depois um pico de temperatura.
    for (int i = 0; i < 3; ++i) {
        auto s = bk.gt.ler(bk.lab.posicao(), bk.lab.heading());
        bk.lab.passo(s);
        if (bk.lab.sensoriou()) {
            P p = bk.lab.posicaoSensoriada();
            int pc = mock_http_post_count();
            tel.movimento(p.x, p.y, bk.lab.paredes(p));
            bk.be.capturarSe(pc);
        }
        mock_timer_advance_us(300'000LL);
        bk.bat.update();
    }

    int pc = mock_http_post_count();
    esp_err_t r = tel.alerta_temperatura(73.5f);
    CHECK_EQ(r, ESP_OK);
    CHECK(bk.be.capturarSe(pc));

    const Pacote* al = bk.be.ultimo();
    REQUIRE(al != nullptr);
    CHECK_EQ(al->tipo, 5);
    CHECK_FLOAT_EQ(al->temp_c, 73.5, 1e-2);

    // O alerta nao apagou o historico: config + ao menos um movimento + alerta.
    CHECK_EQ(bk.be.contar(0), 1);
    CHECK(bk.be.contar(1) >= 1);
    CHECK_EQ(bk.be.contar(5), 1);

    // E os timestamps continuam ordenados apos a injecao do alerta.
    long long anterior = -1;
    bool monotonico = true;
    for (const auto& p : bk.be.recebidos) {
        if (p.ts < anterior) monotonico = false;
        anterior = p.ts;
    }
    CHECK(monotonico);
}

TEST_MAIN()
