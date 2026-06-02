#!/usr/bin/env python3
"""
Simulador de exploração DFS do Micromouse em um labirinto 16x16.

Gera um labirinto aleatório com paredes internas, depois executa uma
exploração DFS (Depth-First Search) célula a célula, enviando cada passo
via HTTP POST para o backend — exatamente como a ESP32 faria.

O frontend recebe cada movimento em tempo real via WebSocket e renderiza
o caminho, as paredes e o rastro de células visitadas.

Uso:
    python3 simulate_maze.py            # labirinto 16x16 (padrão)
    python3 simulate_maze.py --size 8   # labirinto 8x8
    python3 simulate_maze.py --size 4   # labirinto 4x4
    python3 simulate_maze.py --delay 0.3 # mais rápido
"""

import argparse
import json
import random
import sys
import time
import requests

API_URL = "http://localhost:8000/api/telemetria/pacote"

# ---------------------------------------------------------------------------
# Representação do labirinto
# ---------------------------------------------------------------------------

# Bitmask de paredes — mesma convenção da ESP32
NORTH = 1   # bit 0
SOUTH = 2   # bit 1
EAST  = 4   # bit 2
WEST  = 8   # bit 3

OPPOSITE = {NORTH: SOUTH, SOUTH: NORTH, EAST: WEST, WEST: EAST}

# Deslocamento (dx, dy) no sistema do grid:
#   - x cresce para a direita (col)
#   - y cresce para baixo (row) — compatível com como o frontend renderiza
DELTA = {
    NORTH: (0, -1),  # y diminui = sobe no grid
    SOUTH: (0,  1),  # y aumenta = desce no grid
    EAST:  (1,  0),  # x aumenta = direita
    WEST:  (-1, 0),  # x diminui = esquerda
}


def generate_maze(size: int) -> list[list[int]]:
    """Gera um labirinto perfeito (sem ciclos) usando DFS recursivo.

    Retorna uma matriz size×size onde cada célula contém um bitmask
    das paredes presentes (15 = todas as paredes).
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


def dfs_explore(walls: list[list[int]], size: int) -> list[tuple[int, int, int]]:
    """Simula exploração DFS como o Micromouse faria.

    O robô começa em (0, 0) e explora o labirinto usando DFS,
    respeitando as paredes. Retorna a lista de passos na ordem
    em que foram visitados: (x, y, w) onde w é o bitmask de paredes
    da célula naquela posição.

    Inclui backtracking — o robô volta por onde veio quando chega
    a um beco sem saída, exatamente como acontece na realidade.
    """
    visited = [[False] * size for _ in range(size)]
    steps: list[tuple[int, int, int]] = []

    def explore(x: int, y: int):
        visited[y][x] = True
        w = walls[y][x]
        steps.append((x, y, w))

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
    return steps


def send_packet(data: dict, label: str = "") -> bool:
    """Envia um pacote HTTP POST para o backend."""
    try:
        r = requests.post(API_URL, json=data, timeout=5)
        if r.status_code in (200, 201):
            return True
        else:
            print(f"  ⚠ {label} → HTTP {r.status_code}: {r.text[:120]}")
            return False
    except requests.RequestException as e:
        print(f"  ✗ {label} → Erro de conexão: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="Simulador de exploração Micromouse")
    parser.add_argument("--size", type=int, default=16, choices=[4, 8, 16],
                        help="Dimensão do labirinto (padrão: 16)")
    parser.add_argument("--delay", type=float, default=0.5,
                        help="Delay entre passos em segundos (padrão: 0.5)")
    parser.add_argument("--seed", type=int, default=None,
                        help="Seed para reproduzir o mesmo labirinto")
    args = parser.parse_args()

    size = args.size
    delay = args.delay

    if args.seed is not None:
        random.seed(args.seed)

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
    print(f"   ✓ Labirinto {size}×{size} gerado.")
    print()

    # 2. Simular exploração DFS
    print("🔍 Calculando rota de exploração (DFS)...")
    steps = dfs_explore(maze_walls, size)
    unique_cells = len(set((x, y) for x, y, _ in steps))
    print(f"   ✓ {len(steps)} movimentos, {unique_cells} células únicas visitadas.")
    print()

    # 3. Enviar pacote inicial
    print("📡 Enviando pacote INICIAL...")
    initial = {
        "id_corrida": corrida_id,
        "timestamp_ms": 0,
        "dimensao": size,
        "tentativa": 1,
        "bateria": 100.0,
    }
    if not send_packet(initial, "INICIAL"):
        print("   ✗ Falha ao enviar pacote inicial. Backend online?")
        sys.exit(1)
    print("   ✓ Corrida iniciada.")
    print()
    time.sleep(1)

    # 4. Enviar cada passo da exploração
    print(f"🐭 Iniciando exploração ({len(steps)} passos)...")
    print(f"   Acompanhe em tempo real no frontend!")
    print()

    ts = 0
    for i, (x, y, w) in enumerate(steps):
        ts += int(delay * 1000)
        pkt = {
            "id_corrida": corrida_id,
            "timestamp_ms": ts,
            "x": x,
            "y": y,
            "w": w,
        }
        bar_len = 30
        progress = int((i + 1) / len(steps) * bar_len)
        bar = "█" * progress + "░" * (bar_len - progress)
        print(f"\r   [{bar}] {i+1}/{len(steps)}  pos=({x:2d},{y:2d})  w={w:02d}", end="", flush=True)
        send_packet(pkt, f"MOV {i+1}")
        time.sleep(delay)

    print()
    print()

    # 5. Enviar pacote FINAL
    print("🏁 Enviando pacote FINAL...")
    ts += 1000
    final = {
        "id_corrida": corrida_id,
        "timestamp_ms": ts,
        "sucesso": True,
        "v_med": 0.22,
        "bateria": 85.0,
    }
    send_packet(final, "FINAL")
    print("   ✓ Corrida finalizada!")
    print()
    print(f"🎉 Simulação concluída! Total: {unique_cells}/{size*size} células exploradas.")


if __name__ == "__main__":
    main()
