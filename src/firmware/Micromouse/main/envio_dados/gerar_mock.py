#!/usr/bin/env python3
"""Gera um mock de telemetria com um labirinto estruturalmente valido segundo
as regras de main/maze/maze.cpp:

  - Bitmask de paredes: Norte=1, Sul=2, Leste=4, Oeste=8 (enum Parede).
  - Orientacao: y cresce para o Norte (y+1) e x para o Leste (x+1), portanto
    labirinto[0] e a fileira Sul e a coluna x=0 e o Oeste.
  - Reciprocidade: toda parede compartilhada aparece nas duas celulas vizinhas
    (mesmo invariante que atualizarCelula() mantem no firmware).
  - Perimetro totalmente fechado.

O labirinto e gerado como um "labirinto perfeito" (arvore geradora) via DFS
com backtracking, o que garante reciprocidade e perimetro fechado por
construcao. A serializacao replica o formato de enviar_dados_sensores():
campos escalares planos + matriz 2D labirinto[y][x] + labirinto_tamanho.

Uso:
  python gerar_mock.py                       # 16x16, tipo final_mapeamento
  python gerar_mock.py --n 8 --tipo heartbeat
  python gerar_mock.py --saida outro.json
"""
import argparse
import json
import random
import sys

NORTE, SUL, LESTE, OESTE = 1, 2, 4, 8


def gerar_labirinto(n: int, seed: int) -> list[list[int]]:
    # Comeca com todas as celulas totalmente fechadas (15 = N|S|L|O).
    walls = [[NORTE | SUL | LESTE | OESTE for _ in range(n)] for _ in range(n)]
    visitado = [[False] * n for _ in range(n)]
    rng = random.Random(seed)

    # (dx, dy, parede na celula atual, parede reciproca no vizinho)
    direcoes = [
        (0, 1, NORTE, SUL),    # Norte -> vizinho ao norte tem parede Sul
        (0, -1, SUL, NORTE),   # Sul
        (1, 0, LESTE, OESTE),  # Leste -> vizinho a leste tem parede Oeste
        (-1, 0, OESTE, LESTE), # Oeste
    ]

    # DFS iterativo (evita estourar a pilha de recursao em grades grandes).
    pilha = [(0, 0)]
    visitado[0][0] = True
    while pilha:
        x, y = pilha[-1]
        candidatos = []
        for dx, dy, w_self, w_other in direcoes:
            nx, ny = x + dx, y + dy
            if 0 <= nx < n and 0 <= ny < n and not visitado[ny][nx]:
                candidatos.append((nx, ny, w_self, w_other))
        if not candidatos:
            pilha.pop()
            continue
        nx, ny, w_self, w_other = rng.choice(candidatos)
        walls[y][x] &= ~w_self      # abre passagem na celula atual
        walls[ny][nx] &= ~w_other   # ... e a reciproca no vizinho
        visitado[ny][nx] = True
        pilha.append((nx, ny))

    return walls


def validar(walls: list[list[int]]) -> None:
    """Confere os invariantes do maze.cpp; lanca AssertionError se algo quebrar."""
    n = len(walls)
    for y in range(n):
        for x in range(n):
            w = walls[y][x]
            # Perimetro fechado
            if y == 0:
                assert w & SUL, f"({x},{y}) sem parede Sul no perimetro"
            if y == n - 1:
                assert w & NORTE, f"({x},{y}) sem parede Norte no perimetro"
            if x == 0:
                assert w & OESTE, f"({x},{y}) sem parede Oeste no perimetro"
            if x == n - 1:
                assert w & LESTE, f"({x},{y}) sem parede Leste no perimetro"
            # Reciprocidade
            if y + 1 < n:
                assert bool(w & NORTE) == bool(walls[y + 1][x] & SUL), \
                    f"reciprocidade N/S quebrada entre ({x},{y}) e ({x},{y+1})"
            if x + 1 < n:
                assert bool(w & LESTE) == bool(walls[y][x + 1] & OESTE), \
                    f"reciprocidade L/O quebrada entre ({x},{y}) e ({x+1},{y})"


def serializar(payload: dict) -> str:
    """JSON com a matriz formatada uma fileira por linha (legivel e fiel ao formato)."""
    matriz = payload["labirinto"]
    linhas = ",\n    ".join(json.dumps(linha) for linha in matriz)
    cabecalho = {k: v for k, v in payload.items() if k != "labirinto"}
    # Monta manualmente para manter a ordem de insercao do firmware.
    partes = []
    for chave, valor in payload.items():
        if chave == "labirinto":
            partes.append(f'  "labirinto": [\n    {linhas}\n  ]')
        else:
            partes.append(f'  {json.dumps(chave)}: {json.dumps(valor)}')
    return "{\n" + ",\n".join(partes) + "\n}\n"


def main() -> int:
    ap = argparse.ArgumentParser(description="Gera mock de telemetria do Micromouse.")
    ap.add_argument("--n", type=int, default=16, choices=[4, 8, 16],
                    help="Dimensao do labirinto (default 16, igual ao firmware).")
    ap.add_argument("--seed", type=int, default=42, help="Semente do RNG (reprodutivel).")
    ap.add_argument("--tipo", default="final_mapeamento",
                    choices=["avanco_tile", "heartbeat", "inicio_mapeamento",
                             "final_mapeamento", "inicio_corrida", "final_corrida"])
    ap.add_argument("--saida", default="mock_envio.json", help="Arquivo de saida.")
    args = ap.parse_args()

    walls = gerar_labirinto(args.n, args.seed)
    validar(walls)

    payload = {
        "tipo": args.tipo,
        "velocidade_media_cms": 18.5,
        "direcao": "N",
        "temperatura": 27.4,
        "soc": 86,
        "timestamp_ms": 123456789,
        "labirinto": walls,
        "labirinto_tamanho": args.n,
    }

    with open(args.saida, "w", encoding="utf-8") as f:
        f.write(serializar(payload))

    print(f"OK: {args.saida} gerado ({args.n}x{args.n}, tipo={args.tipo}). "
          f"Invariantes do maze.cpp validados.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
