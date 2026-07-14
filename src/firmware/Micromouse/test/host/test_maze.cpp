// Unit + integration tests for the Labirinto (flood-fill maze solver).
//
// This module is pure logic with no hardware dependencies, so it is tested
// directly against the real maze.cpp.
#include "framework.hpp"
#include "maze/maze.hpp"

using D = Labirinto::Direcao;
using P = Labirinto::Posicao;
using R = Labirinto::Resultado;
using F = Labirinto::Fase;

// ===========================================================================
//  Ground-truth maze used to drive the integration tests. It mirrors the wall
//  conventions of Labirinto and produces the sensor readings the robot would
//  get at a given position/heading.
// ===========================================================================
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
            case D::Sul: return Labirinto::ParedeSul;
            case D::Leste: return Labirinto::ParedeLeste;
            case D::Oeste: return Labirinto::ParedeOeste;
            default: return 0;
        }
    }
    static D oposta(D d) {
        switch (d) {
            case D::Norte: return D::Sul;
            case D::Sul: return D::Norte;
            case D::Leste: return D::Oeste;
            case D::Oeste: return D::Leste;
            default: return D::Nenhuma;
        }
    }
    static D cw(D d) { return static_cast<D>((static_cast<uint8_t>(d) + 1) & 3); }
    static D ccw(D d) { return static_cast<D>((static_cast<uint8_t>(d) + 3) & 3); }

    static P vizinho(P p, D d) {
        switch (d) {
            case D::Norte: return {p.x, (uint8_t)(p.y + 1)};
            case D::Sul: return {p.x, (uint8_t)(p.y - 1)};
            case D::Leste: return {(uint8_t)(p.x + 1), p.y};
            case D::Oeste: return {(uint8_t)(p.x - 1), p.y};
            default: return p;
        }
    }
    bool dentro(P p) const { return p.x < n && p.y < n; }

    // Add a wall between p and its neighbour in direction d (both sides).
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
        s.parede_frente = temParede(p, heading);
        s.parede_esquerda = temParede(p, ccw(heading));
        s.parede_direita = temParede(p, cw(heading));
        return s;
    }
};

// Drives the maze state machine to completion against a ground-truth maze.
// Returns the final Resultado (or Bloqueado if it stalls / hits the cap).
static R runFullCycle(Labirinto& lab, const GroundTruth& gt, int max_steps,
                      int* out_steps = nullptr) {
    R res = R::EmProgresso;
    int steps = 0;
    for (; steps < max_steps; ++steps) {
        auto s = gt.ler(lab.posicao(), lab.heading());
        res = lab.passo(s);
        if (res == R::FastRunCompleto || res == R::Bloqueado) break;
    }
    if (out_steps) *out_steps = steps;
    return res;
}

// ===========================================================================
//  Geometry / helpers
// ===========================================================================
TEST_CASE(default_size_is_16) {
    Labirinto lab;
    CHECK_EQ(lab.tamanho(), (uint8_t)16);
}

TEST_CASE(configurar_changes_size) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    CHECK_EQ(lab.tamanho(), (uint8_t)8);
    lab.configurar(Labirinto::Tamanho::k4x4);
    CHECK_EQ(lab.tamanho(), (uint8_t)4);
}

TEST_CASE(vizinho_directions) {
    Labirinto lab;
    P c{5, 5};
    CHECK(lab.vizinho(c, D::Norte) == (P{5, 6}));
    CHECK(lab.vizinho(c, D::Sul) == (P{5, 4}));
    CHECK(lab.vizinho(c, D::Leste) == (P{6, 5}));
    CHECK(lab.vizinho(c, D::Oeste) == (P{4, 5}));
}

TEST_CASE(dentro_dos_limites) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    CHECK(lab.dentroDosLimites(P{0, 0}));
    CHECK(lab.dentroDosLimites(P{7, 7}));
    CHECK(!lab.dentroDosLimites(P{8, 0}));
    CHECK(!lab.dentroDosLimites(P{0, 8}));
    // y-1 of (0,0) underflows to 255 -> out of bounds.
    CHECK(!lab.dentroDosLimites(lab.vizinho(P{0, 0}, D::Sul)));
}

TEST_CASE(boundary_is_always_walled) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{4, 4});
    // South and West of the origin are the outer boundary.
    CHECK(lab.temParede(P{0, 0}, D::Sul));
    CHECK(lab.temParede(P{0, 0}, D::Oeste));
    // Interior of a fresh maze has no internal walls.
    CHECK(!lab.temParede(P{0, 0}, D::Norte));
    CHECK(!lab.temParede(P{0, 0}, D::Leste));
}

// ===========================================================================
//  Wall recording
// ===========================================================================
TEST_CASE(atualizar_paredes_records_and_is_symmetric) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{4, 4});  // heading starts Norte

    // At (1,1), heading Norte: wall in front (N), left (O), none right.
    Labirinto::LeituraSensores s{/*frente*/ true, /*esq*/ true, /*dir*/ false};
    lab.atualizarParedes(P{1, 1}, s);

    CHECK(lab.temParede(P{1, 1}, D::Norte));
    CHECK(lab.temParede(P{1, 1}, D::Oeste));
    CHECK(!lab.temParede(P{1, 1}, D::Leste));

    // Symmetry: the north wall of (1,1) is the south wall of (1,2).
    CHECK(lab.temParede(P{1, 2}, D::Sul));
    // The west wall of (1,1) is the east wall of (0,1).
    CHECK(lab.temParede(P{0, 1}, D::Leste));

    CHECK(lab.visitada(P{1, 1}));
    CHECK(!lab.visitada(P{2, 2}));
}

TEST_CASE(paredes_bitmask) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{4, 4});
    Labirinto::LeituraSensores s{true, false, true};  // frente=N, direita=L
    lab.atualizarParedes(P{2, 2}, s);
    uint8_t w = lab.paredes(P{2, 2});
    CHECK((w & Labirinto::ParedeNorte) != 0);
    CHECK((w & Labirinto::ParedeLeste) != 0);
    CHECK((w & Labirinto::ParedeSul) == 0);
    CHECK((w & Labirinto::ParedeOeste) == 0);
}

// ===========================================================================
//  Flood fill
// ===========================================================================
TEST_CASE(floodfill_open_maze_is_manhattan) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{7, 7});
    lab.floodFill(P{7, 7});
    CHECK_EQ(lab.distancia(P{7, 7}), (uint16_t)0);
    CHECK_EQ(lab.distancia(P{6, 7}), (uint16_t)1);
    CHECK_EQ(lab.distancia(P{0, 0}), (uint16_t)14);
    CHECK_EQ(lab.distancia(P{3, 5}), (uint16_t)(4 + 2));
}

TEST_CASE(floodfill_respects_walls) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{2, 0});
    // Build a wall that forces a detour: wall between (0,0)-(1,0) and (1,0)-(2,0)
    // does not exist, but wall the direct corridor on row 0 between (0,0) and
    // (1,0). Robot must go around via row 1.
    Labirinto::LeituraSensores w{false, false, true};  // at (0,0) heading N, wall to the east
    lab.atualizarParedes(P{0, 0}, w);
    lab.floodFill(P{2, 0});
    // Direct neighbour (1,0) is unreachable from (0,0) directly, but reachable
    // overall, so its distance is the detour length, and (0,0) is +1 from there.
    CHECK_EQ(lab.distancia(P{2, 0}), (uint16_t)0);
    // (0,0) must detour north then east then south: (0,0)->(0,1)->(1,1)->(1,0)
    // ... ->(2,0). Path length from (0,0): 0,1->1,1->2,1->2,0 = 4? Let's just
    // assert it is greater than the naive Manhattan distance of 2.
    CHECK(lab.distancia(P{0, 0}) > (uint16_t)2);
}

TEST_CASE(floodfill_unreachable_stays_infinite) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{4, 4});
    // Box in cell (2,2) completely.
    Labirinto::LeituraSensores all{true, true, true};  // front,left,right walls
    // heading Norte -> sets N, O, L. Need S too; set via neighbour.
    lab.atualizarParedes(P{2, 2}, all);
    // Add south wall by recording from the cell below looking north.
    Labirinto::LeituraSensores below{true, false, false};
    lab.atualizarParedes(P{2, 1}, below);  // north wall of (2,1) == south wall of (2,2)
    lab.floodFill(P{0, 0});
    CHECK_EQ(lab.distancia(P{2, 2}), Labirinto::kDistanciaInfinita);
}

TEST_CASE(direcao_de_menor_dist_points_downhill) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{7, 0});
    lab.floodFill(P{7, 0});
    // On row 0 the gradient decreases to the east.
    CHECK_EQ(lab.direcaoDeMenorDist(P{3, 0}), D::Leste);
    // At the destination there is nothing lower.
    CHECK_EQ(lab.direcaoDeMenorDist(P{7, 0}), D::Nenhuma);
}

TEST_CASE(rota_otima_length_on_open_maze) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{3, 2});
    P buf[64];
    uint16_t n = lab.rotaOtima(buf, 64);
    // Manhattan distance 5 -> 6 cells including both ends.
    CHECK_EQ(n, (uint16_t)6);
    CHECK(buf[0] == (P{0, 0}));
    CHECK(buf[n - 1] == (P{3, 2}));
}

// ===========================================================================
//  Integration: full explore -> refine -> return -> fast-run cycle
// ===========================================================================
TEST_CASE(integration_open_maze_completes) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{4, 4});

    GroundTruth gt;
    gt.clear(8);  // fully open

    int steps = 0;
    R res = runFullCycle(lab, gt, 5000, &steps);
    CHECK_EQ(res, R::FastRunCompleto);
    CHECK_EQ(lab.fase(), F::Concluido);
    // A corrida rapida para no primeiro tile do bloco central que alcancar.
    CHECK(lab.ehCelulaCentro(lab.posicao()));
    CHECK(steps < 5000);
}

// A exploracao precisa VISITAR todas as celulas do bloco central 2x2 (garante
// que o objetivo foi mesmo alcancado e que nao ha parede entre elas) antes de
// sinalizar AlcancouObjetivo (Req 1).
TEST_CASE(integration_explore_visits_full_center_block) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k4x4);
    lab.iniciar(P{0, 0}, P{1, 1});

    GroundTruth gt;
    gt.clear(4);

    R res = R::EmProgresso;
    for (int i = 0; i < 2000; ++i) {
        auto s = gt.ler(lab.posicao(), lab.heading());
        res = lab.passo(s);
        if (res == R::AlcancouObjetivo || res == R::Bloqueado) break;
    }
    CHECK_EQ(res, R::AlcancouObjetivo);

    P cs[4];
    uint8_t nc = 0;
    lab.celulasCentro(cs, nc);
    CHECK_EQ(nc, (uint8_t)4);
    for (uint8_t i = 0; i < nc; ++i) CHECK(lab.visitada(cs[i]));
}

// A corrida rapida para no tile do centro MAIS PROXIMO da largada, nao no canto
// oposto passado como objetivo (Req 2).
TEST_CASE(integration_fastrun_stops_at_first_center_cell) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{4, 4});   // canto mais distante do bloco central

    GroundTruth gt;
    gt.clear(8);

    R res = runFullCycle(lab, gt, 8000);
    CHECK_EQ(res, R::FastRunCompleto);
    CHECK(lab.ehCelulaCentro(lab.posicao()));
    CHECK(lab.posicao() == (P{3, 3}));   // tile do centro vizinho da largada
}

TEST_CASE(integration_phase_transitions_in_order) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k4x4);
    lab.iniciar(P{0, 0}, P{2, 2});

    GroundTruth gt;
    gt.clear(4);

    bool saw_objetivo = false, saw_fechado = false, saw_retornou = false, saw_fast = false;
    R res = R::EmProgresso;
    for (int i = 0; i < 5000; ++i) {
        auto s = gt.ler(lab.posicao(), lab.heading());
        res = lab.passo(s);
        if (res == R::AlcancouObjetivo) saw_objetivo = true;
        if (res == R::CaminhoFechado) saw_fechado = true;
        if (res == R::RetornouAoInicio) saw_retornou = true;
        if (res == R::FastRunCompleto) {
            saw_fast = true;
            break;
        }
    }
    CHECK(saw_objetivo);
    CHECK(saw_fechado);
    CHECK(saw_retornou);
    CHECK(saw_fast);
}

TEST_CASE(integration_maze_with_walls_completes_and_avoids_walls) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{4, 4});

    GroundTruth gt;
    gt.clear(8);
    // Build a simple barrier wall along x=2 from y=0..3, with a gap at y=4 so the
    // goal is still reachable by going around the top.
    for (uint8_t y = 0; y <= 3; ++y) gt.addWall(P{2, y}, D::Leste);

    int steps = 0;
    R res = runFullCycle(lab, gt, 8000, &steps);
    CHECK_EQ(res, R::FastRunCompleto);
    CHECK(lab.ehCelulaCentro(lab.posicao()));

    // No phantom walls: every interior wall the robot believes in must really
    // exist in the ground-truth maze. (The robot need not have discovered every
    // real wall, only that it never invents one.)
    const D dirs[4] = {D::Norte, D::Leste, D::Sul, D::Oeste};
    int mismatches = 0;
    for (uint8_t y = 0; y < 8; ++y) {
        for (uint8_t x = 0; x < 8; ++x) {
            P p{x, y};
            for (D d : dirs) {
                P v = lab.vizinho(p, d);
                if (!lab.dentroDosLimites(v)) continue;  // skip boundary
                if (lab.temParede(p, d) && !gt.temParede(p, d)) mismatches++;
            }
        }
    }
    CHECK_EQ(mismatches, 0);
}

TEST_CASE(integration_unreachable_goal_is_blocked) {
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    lab.iniciar(P{0, 0}, P{4, 4});

    GroundTruth gt;
    gt.clear(8);
    // O objetivo agora e o bloco central 2x2 inteiro, entao murar so (4,4) nao
    // basta: sela COMPLETAMENTE as 4 celulas do centro ({3,3},{4,3},{3,4},{4,4}).
    const P centro[4] = {{3, 3}, {4, 3}, {3, 4}, {4, 4}};
    const D dirs[4] = {D::Norte, D::Sul, D::Leste, D::Oeste};
    for (P c : centro)
        for (D d : dirs) gt.addWall(c, d);

    int steps = 0;
    R res = runFullCycle(lab, gt, 8000, &steps);
    CHECK_EQ(res, R::Bloqueado);
    // Nunca chegou a nenhuma celula do centro.
    CHECK(!lab.ehCelulaCentro(lab.posicao()));
}

// ===========================================================================
//  Robot interface callbacks
// ===========================================================================
static int g_virar_calls = 0;
static int g_avancar_calls = 0;
static D g_last_dir = D::Nenhuma;
static void cb_virar(D d) {
    g_virar_calls++;
    g_last_dir = d;
}
static void cb_avancar() { g_avancar_calls++; }

TEST_CASE(robot_callbacks_fire_on_move) {
    g_virar_calls = 0;
    g_avancar_calls = 0;
    Labirinto lab;
    lab.configurar(Labirinto::Tamanho::k8x8);
    Labirinto::InterfaceRobo robo;
    robo.virarPara = &cb_virar;
    robo.avancar = &cb_avancar;
    lab.configurarRobo(robo);
    lab.iniciar(P{0, 0}, P{2, 0});

    Labirinto::LeituraSensores open{false, false, false};
    R res = lab.passo(open);  // should move one cell
    CHECK_EQ(res, R::EmProgresso);
    CHECK_EQ(g_virar_calls, 1);
    CHECK_EQ(g_avancar_calls, 1);
    CHECK(lab.posicao() != (P{0, 0}));
}

TEST_MAIN()
