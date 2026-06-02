#!/usr/bin/env python3
"""
Simulador de exploração DFS do Micromouse em um labirinto NxN.

Gera um labirinto aleatório com paredes internas e um objetivo 2×2
posicionado aleatoriamente, depois executa uma exploração DFS
(Depth-First Search) célula a célula, enviando cada passo via HTTP
POST para o backend — exatamente como a ESP32 faria.

O frontend recebe cada movimento em tempo real via WebSocket e renderiza
o caminho, as paredes e o rastro de células visitadas.

Pacotes seguem a especificação do telemetria.md:
  tipo=0  →  Configuração Inicial
  tipo=1  →  Movimentação / Descoberta de Paredes
  tipo=2  →  Rota Otimizada
  tipo=3  →  Fim de Corrida

Coordenadas seguem o sistema cartesiano:
  - Origem (0,0) no canto inferior esquerdo
  - X cresce para a direita
  - Y cresce para cima

Uso:
    python3 simulate_maze.py            # labirinto 16x16 (padrão)
    python3 simulate_maze.py --size 8   # labirinto 8x8
    python3 simulate_maze.py --size 4   # labirinto 4x4
    python3 simulate_maze.py --delay 0.3 # mais rápido
"""

import argparse
import random
import sys
import time
from collections import deque

import requests

API_URL = "http://localhost:8000/api/telemetria/pacote"

# ---------------------------------------------------------------------------
# Representação do labirinto
# ---------------------------------------------------------------------------

# Bitmask de paredes — mesma convenção da ESP32 e do telemetria.md
NORTH = 1   # bit 0
SOUTH = 2   # bit 1
EAST  = 4   # bit 2
WEST  = 8   # bit 3

OPPOSITE = {NORTH: SOUTH, SOUTH: NORTH, EAST: WEST, WEST: EAST}

# Deslocamento (dx, dy) no sistema cartesiano:
#   - x cresce para a direita (Leste)
#   - y cresce para cima (Norte) — conforme telemetria.md
DELTA = {
    NORTH: (0,  -1),  # Norte = y++ (sobe)
    SOUTH: (0, 1),  # Sul   = y-- (desce)
    EAST:  (1,  0),  # Leste = x++ (direita)
    WEST:  (-1, 0),  # Oeste = x-- (esquerda)
}


# ---------------------------------------------------------------------------
# Objetivo 2×2 randomizado
# ---------------------------------------------------------------------------

def choose_goal_2x2(size: int) -> list[tuple[int, int]]:
    """Escolhe aleatoriamente um bloco 2×2 como objetivo no labirinto.

    Evita colocar o objetivo tocando as bordas externas do labirinto,
    a menos que o labirinto seja pequeno (4x4).
    Retorna lista de 4 tuplas (x, y) — canto inferior-esquerdo do bloco
    é (gx, gy), cobrindo (gx, gy), (gx+1, gy), (gx, gy+1), (gx+1, gy+1).
    """
    if size <= 4:
        # Para labirinto 4x4, o único 2x2 que não toca as bordas externas
        # não existe perfeitamente se considerarmos todas as bordas, 
        # mas o centro exato é (1,1).
        gx, gy = 1, 1
        return [(gx, gy), (gx + 1, gy), (gx, gy + 1), (gx + 1, gy + 1)]

    candidates = []
    # range de 1 até size-3 garante que:
    # gx mínimo = 1 (não toca a borda esquerda 0)
    # gx máximo = size-3 -> as células usadas serão size-3 e size-2. 
    # A borda direita é size-1, então não toca.
    for gx in range(1, size - 2):
        for gy in range(1, size - 2):
            cells = [(gx, gy), (gx + 1, gy), (gx, gy + 1), (gx + 1, gy + 1)]
            # O bloco não pode incluir a partida (0, 0)
            if (0, 0) not in cells:
                candidates.append(cells)
                
    if not candidates:
        gx, gy = 1, 1
        return [(gx, gy), (gx + 1, gy), (gx, gy + 1), (gx + 1, gy + 1)]
        
    return random.choice(candidates)


def open_goal_walls(walls: list[list[int]], goal_cells: list[tuple[int, int]]) -> None:
    """Remove as paredes internas entre as 4 células do objetivo 2×2.

    Isso cria uma câmara aberta no objetivo, como nos labirintos
    de competição Micromouse reais.
    """
    goal_set = set(goal_cells)
    for (x, y) in goal_cells:
        for d, (dx, dy) in DELTA.items():
            nx, ny = x + dx, y + dy
            if (nx, ny) in goal_set:
                # Remove parede entre (x,y) e (nx,ny)
                walls[y][x] &= ~d
                walls[ny][nx] &= ~OPPOSITE[d]


# ---------------------------------------------------------------------------
# Geração do labirinto
# ---------------------------------------------------------------------------

def generate_maze(size: int) -> list[list[int]]:
    """Gera um labirinto perfeito (sem ciclos) usando DFS recursivo.

    Retorna uma matriz size×size onde cada célula contém um bitmask
    das paredes presentes (15 = todas as paredes).

    A indexação é walls[y][x] com y=0 no canto inferior.
    """
    # Começa com todas as paredes fechadas
    walls = [[NORTH | SOUTH | EAST | WEST for _ in range(size)] for _ in range(size)]
    visited = [[False] * size for _ in range(size)]

    def carve(x: int, y: int):
        visited[y][x] = True
        dirs = [NORTH, SOUTH, EAST, WEST]
        random.shuffle(dirs)
        for d in dirs:
            dx, dy = DELTA[d]
            nx, ny = x + dx, y + dy
            if 0 <= nx < size and 0 <= ny < size and not visited[ny][nx]:
                # Remove a parede entre a célula atual e a vizinha
                walls[y][x] &= ~d
                walls[ny][nx] &= ~OPPOSITE[d]
                carve(nx, ny)

    carve(0, 0)
    return walls


# ---------------------------------------------------------------------------
# Exploração DFS com parada no objetivo
# ---------------------------------------------------------------------------

def dfs_explore(
    walls: list[list[int]],
    size: int,
    goal_cells: list[tuple[int, int]],
) -> tuple[list[tuple[int, int, int]], bool]:
    """Simula exploração DFS como o Micromouse faria.

    O robô começa em (0, 0) e explora o labirinto usando DFS,
    respeitando as paredes. A exploração MAPEIA TUDO e não para
    quando o robô atinge o objetivo.

    Retorna:
      - Lista de passos (x, y, w) incluindo backtracking.
      - True se o objetivo foi alcançado, False caso contrário.
    """
    goal_set = set(goal_cells)
    visited = [[False] * size for _ in range(size)]
    steps: list[tuple[int, int, int]] = []
    reached_goal = False

    def explore(x: int, y: int) -> None:
        nonlocal reached_goal
        visited[y][x] = True
        w = walls[y][x]
        steps.append((x, y, w))

        # Chegou no objetivo?
        if (x, y) in goal_set:
            reached_goal = True

        dirs = [NORTH, SOUTH, EAST, WEST]
        random.shuffle(dirs)
        for d in dirs:
            # Só pode ir se NÃO há parede naquela direção
            if w & d:
                continue
            dx, dy = DELTA[d]
            nx, ny = x + dx, y + dy
            if 0 <= nx < size and 0 <= ny < size and not visited[ny][nx]:
                explore(nx, ny)
                # Backtrack — volta para a célula anterior
                steps.append((x, y, w))

    explore(0, 0)
    return steps, reached_goal


# ---------------------------------------------------------------------------
# BFS para encontrar a rota otimizada (caminho mais curto)
# ---------------------------------------------------------------------------

def bfs_shortest_path(
    walls: list[list[int]],
    size: int,
    start: tuple[int, int],
    goal_cells: list[tuple[int, int]],
) -> list[list[int]]:
    """Calcula o caminho mais curto de start até qualquer célula do objetivo.

    Usa BFS (Breadth-First Search) para encontrar a rota ótima.
    Retorna lista de coordenadas [[x, y], ...] para enviar como
    pacote tipo=2 (Rota Otimizada).
    """
    goal_set = set(goal_cells)
    visited = [[False] * size for _ in range(size)]
    parent: dict[tuple[int, int], tuple[int, int] | None] = {}

    sx, sy = start
    queue: deque[tuple[int, int]] = deque()
    queue.append((sx, sy))
    visited[sy][sx] = True
    parent[(sx, sy)] = None

    end_cell = None

    while queue:
        x, y = queue.popleft()
        if (x, y) in goal_set:
            end_cell = (x, y)
            break

        w = walls[y][x]
        for d, (dx, dy) in DELTA.items():
            if w & d:
                continue  # Parede bloqueia
            nx, ny = x + dx, y + dy
            if 0 <= nx < size and 0 <= ny < size and not visited[ny][nx]:
                visited[ny][nx] = True
                parent[(nx, ny)] = (x, y)
                queue.append((nx, ny))

    if end_cell is None:
        return []  # Sem caminho (não deveria acontecer)

    # Reconstruir caminho de trás para frente
    path: list[list[int]] = []
    current: tuple[int, int] | None = end_cell
    while current is not None:
        path.append([current[0], current[1]])
        current = parent[current]
    path.reverse()
    return path


# ---------------------------------------------------------------------------
# Impressão visual do labirinto no terminal
# ---------------------------------------------------------------------------

def print_maze_ascii(
    walls: list[list[int]],
    size: int,
    goal_cells: list[tuple[int, int]],
) -> None:
    """Imprime o labirinto no terminal com representação ASCII.

    Mostra S para o início, G para as células do objetivo,
    e as paredes como caracteres +, -, |.
    """
    goal_set = set(goal_cells)

    # Renderiza de cima para baixo (y mais alto primeiro)
    for y in range(size - 1, -1, -1):
        # Linha superior (paredes norte)
        line_top = ""
        for x in range(size):
            line_top += "+"
            if walls[y][x] & NORTH:
                line_top += "---"
            else:
                line_top += "   "
        line_top += "+"
        print(f"   {line_top}")

        # Linha do meio (paredes oeste/leste + conteúdo)
        line_mid = ""
        for x in range(size):
            if walls[y][x] & WEST:
                line_mid += "|"
            else:
                line_mid += " "

            if (x, y) == (0, 0):
                line_mid += " S "
            elif (x, y) in goal_set:
                line_mid += " G "
            else:
                line_mid += "   "
        # Parede leste da última coluna
        if walls[y][size - 1] & EAST:
            line_mid += "|"
        else:
            line_mid += " "
        print(f"   {line_mid}")

    # Linha inferior (paredes sul da linha y=0)
    line_bottom = ""
    for x in range(size):
        line_bottom += "+"
        if walls[0][x] & SOUTH:
            line_bottom += "---"
        else:
            line_bottom += "   "
    line_bottom += "+"
    print(f"   {line_bottom}")


# ---------------------------------------------------------------------------
# Comunicação com o backend
# ---------------------------------------------------------------------------

def send_packet(data: dict, label: str = "") -> bool:
    """Envia um pacote HTTP POST para o backend."""
    try:
        r = requests.post(API_URL, json=data, timeout=5)
        if r.status_code in (200, 201):
            return True
        else:
            print(f"  ⚠ {label} → HTTP {r.status_code}: {r.text[:200]}")
            return False
    except requests.RequestException as e:
        print(f"  ✗ {label} → Erro de conexão: {e}")
        return False


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Simulador de exploração Micromouse")
    parser.add_argument("--size", type=int, default=16, choices=[4, 8, 16],
                        help="Dimensão do labirinto (padrão: 16)")
    parser.add_argument("--delay", type=float, default=0.5,
                        help="Delay entre passos em segundos (padrão: 0.5)")
    parser.add_argument("--seed", type=int, default=None,
                        help="Seed para reproduzir o mesmo labirinto")
    parser.add_argument("--show-maze", action="store_true",
                        help="Exibe o labirinto ASCII no terminal")
    args = parser.parse_args()

    size = args.size
    delay = args.delay

    if args.seed is not None:
        random.seed(args.seed)

    sys.setrecursionlimit(size * size + 100)

    # Gera um ID de corrida único baseado no timestamp
    corrida_id = int(time.time()) % 100000

    print(f"╔══════════════════════════════════════════╗")
    print(f"║   Simulador Micromouse — {size}×{size}            ║")
    print(f"║   ID corrida: {corrida_id:<26} ║")
    print(f"║   Delay: {delay}s entre passos            ║")
    print(f"╚══════════════════════════════════════════╝")
    print()

    # 1. Gerar labirinto
    print("🏗  Gerando labirinto...")
    maze_walls = generate_maze(size)

    # 2. Escolher objetivo 2×2 aleatório
    goal_cells = choose_goal_2x2(size)
    gx_min = min(c[0] for c in goal_cells)
    gy_min = min(c[1] for c in goal_cells)
    print(f"   ✓ Labirinto {size}×{size} gerado.")
    print(f"   🎯 Objetivo 2×2 em: ({gx_min},{gy_min})–({gx_min+1},{gy_min+1})")
    print(f"      Células: {goal_cells}")

    # 3. Abrir paredes internas do objetivo
    open_goal_walls(maze_walls, goal_cells)
    print(f"   ✓ Paredes internas do objetivo removidas.")
    print()

    if args.show_maze:
        print("🗺  Labirinto gerado:")
        print_maze_ascii(maze_walls, size, goal_cells)
        print()

    # 4. Simular exploração DFS (para ao atingir o objetivo)
    print("🔍 Calculando rota de exploração (DFS)...")
    steps, reached_goal = dfs_explore(maze_walls, size, goal_cells)
    unique_cells = len(set((x, y) for x, y, _ in steps))
    status_str = "✓ OBJETIVO ALCANÇADO" if reached_goal else "✗ Objetivo NÃO alcançado"
    print(f"   {status_str}")
    print(f"   {len(steps)} movimentos, {unique_cells} células únicas visitadas.")
    print()

    # 5. Calcular rota otimizada (BFS)
    print("🧠 Calculando rota otimizada (BFS)...")
    optimal_route = bfs_shortest_path(maze_walls, size, (0, 0), goal_cells)
    print(f"   ✓ Rota ótima: {len(optimal_route)} passos.")
    print()

    # 6. Enviar pacote INICIAL (tipo=0) conforme telemetria.md
    print("📡 Enviando pacote INICIAL (tipo=0)...")
    initial = {
        "tipo": 0,
        "timestamp_ms": 0,
        "dimensao": size,
        "bateria": 100,
    }
    if not send_packet(initial, "INICIAL"):
        print("   ✗ Falha ao enviar pacote inicial. Backend online?")
        sys.exit(1)
    print("   ✓ Corrida iniciada.")
    print()
    time.sleep(1)

    # 7. Enviar cada passo da exploração (tipo=1) conforme telemetria.md
    print(f"🐭 Iniciando exploração ({len(steps)} passos)...")
    print(f"   Acompanhe em tempo real no frontend!")
    print()

    ts = 0
    for i, (x, y, w) in enumerate(steps):
        ts += int(delay * 1000)
        pkt = {
            "tipo": 1,
            "timestamp_ms": ts,
            "x": x,
            "y": y,
            "w": w,
        }
        bar_len = 30
        progress = int((i + 1) / len(steps) * bar_len)
        bar = "█" * progress + "░" * (bar_len - progress)

        in_goal = "🎯" if (x, y) in set(goal_cells) else "  "
        print(f"\r   [{bar}] {i+1}/{len(steps)}  pos=({x:2d},{y:2d})  w={w:02d} {in_goal}", end="", flush=True)
        send_packet(pkt, f"MOV {i+1}")
        time.sleep(delay)

    print()
    print()

    # 8. Enviar rota otimizada (tipo=2) conforme telemetria.md
    print(f"🧭 Enviando ROTA OTIMIZADA (tipo=2) — {len(optimal_route)} pontos...")
    ts += 1000
    rota_pkt = {
        "tipo": 2,
        "timestamp_ms": ts,
        "rota": optimal_route,
    }
    send_packet(rota_pkt, "ROTA")
    print("   ✓ Rota otimizada enviada.")
    print()

    # 9. Enviar pacote FINAL (tipo=3) conforme telemetria.md
    print("🏁 Enviando pacote FINAL (tipo=3)...")
    ts += 1000
    final = {
        "tipo": 3,
        "timestamp_ms": ts,
        "sucesso": reached_goal,
        "v_med": 0.22,
        "bateria": 85,
    }
    send_packet(final, "FINAL")

    if reached_goal:
        print("   ✓ Corrida finalizada com SUCESSO!")
    else:
        print("   ✗ Corrida finalizada — objetivo não alcançado.")
    print()
    print(f"🎉 Simulação concluída! {unique_cells}/{size*size} células exploradas.")
    print(f"   Rota ótima: {len(optimal_route)} passos.")


if __name__ == "__main__":
    main()
