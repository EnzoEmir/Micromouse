#!/bin/bash
# Atalho para rodar o simulador Python do Micromouse.
# Aceita os mesmos argumentos: --size 4|8|16  --delay N  --seed N

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
python3 "$SCRIPT_DIR/simulate_maze.py" "$@"
