import type { Cell, Direction, Position } from "./types";

// Cria o labirinto inicial com paredes externas fechadas.
export const createMaze = (size: number): Cell[][] => {
  return Array.from({ length: size }, (_, row) =>
    Array.from({ length: size }, (_, col) => {
      const isTop = row === 0;
      const isBottom = row === size - 1;
      const isLeft = col === 0;
      const isRight = col === size - 1;

      return {
        visited: false,
        historyStep: null,
        walls: {
          north: isTop,
          south: isBottom,
          west: isLeft,
          east: isRight,
        },
      };
    }),
  );
};

// Retorna a direcao oposta (para espelhar paredes).
export const getOppositeDirection = (direction: Direction): Direction => {
  switch (direction) {
    case "north":
      return "south";
    case "south":
      return "north";
    case "east":
      return "west";
    case "west":
      return "east";
  }
};

// Calcula a proxima posicao a partir de uma direcao.
export const stepFromPosition = (
  position: Position,
  direction: Direction,
): Position => {
  switch (direction) {
    case "north":
      return { row: position.row - 1, col: position.col };
    case "south":
      return { row: position.row + 1, col: position.col };
    case "east":
      return { row: position.row, col: position.col + 1 };
    case "west":
      return { row: position.row, col: position.col - 1 };
  }
};

// Valida se a posicao esta dentro do grid.
export const isInsideMaze = (position: Position, size: number): boolean => {
  return (
    position.row >= 0 &&
    position.row < size &&
    position.col >= 0 &&
    position.col < size
  );
};

// Marca parede detectada na celula atual e na vizinha correspondente.
export const markWall = (
  maze: Cell[][],
  position: Position,
  direction: Direction,
): Cell[][] => {
  const next = maze.map((row) =>
    row.map((cell) => ({ ...cell, walls: { ...cell.walls } })),
  );
  const cell = next[position.row]?.[position.col];
  if (!cell) {
    return next;
  }

  cell.walls[direction] = true;
  const neighbor = stepFromPosition(position, direction);
  if (next[neighbor.row]?.[neighbor.col]) {
    next[neighbor.row][neighbor.col].walls[getOppositeDirection(direction)] =
      true;
  }

  return next;
};

// Marca a celula como visitada e registra o passo do historico.
export const markVisited = (
  maze: Cell[][],
  position: Position,
  step: number,
): Cell[][] => {
  const next = maze.map((row) =>
    row.map((cell) => ({ ...cell, walls: { ...cell.walls } })),
  );
  const cell = next[position.row]?.[position.col];
  if (!cell) {
    return next;
  }

  cell.visited = true;
  cell.historyStep = step;
  return next;
};

export const hasWallBetween = (
  maze: Cell[][],
  p1: Position,
  p2: Position,
): boolean => {
  const cell1 = maze[p1.row]?.[p1.col];
  const cell2 = maze[p2.row]?.[p2.col];
  if (!cell1 && !cell2) return false;

  if (p2.col === p1.col + 1 && p2.row === p1.row) {
    return cell1?.walls?.east || cell2?.walls?.west || false;
  }
  if (p2.col === p1.col - 1 && p2.row === p1.row) {
    return cell1?.walls?.west || cell2?.walls?.east || false;
  }
  if (p2.row === p1.row + 1 && p2.col === p1.col) {
    return cell1?.walls?.south || cell2?.walls?.north || false;
  }
  if (p2.row === p1.row - 1 && p2.col === p1.col) {
    return cell1?.walls?.north || cell2?.walls?.south || false;
  }

  return true; // Not adjacent, consider it walled to prevent direct crossing
};

/**
 * Normaliza um array de posições para que o trajeto siga apenas
 * movimentos ortogonais (horizontal e vertical), sem linhas diagonais.
 *
 * Quando dois pontos consecutivos diferem tanto em row quanto em col,
 * um ponto intermediário é inserido para criar um caminho em "L".
 * Usa o labirinto (se fornecido) para escolher o caminho intermediário
 * que não atravessa paredes.
 */
export const normalizePathToOrthogonal = (
  points: Position[],
  maze?: Cell[][],
): Position[] => {
  if (points.length <= 1) {
    return [...points];
  }

  const normalized: Position[] = [];

  for (let i = 0; i < points.length - 1; i++) {
    const current = points[i];
    const next = points[i + 1];

    normalized.push(current);

    const movedInRow = current.row !== next.row;
    const movedInCol = current.col !== next.col;

    if (movedInRow && movedInCol) {
      const p1 = { row: current.row, col: next.col }; // Move col first
      const p2 = { row: next.row, col: current.col }; // Move row first

      let useP1 = true;

      if (maze) {
        // Check which intermediate point avoids walls
        const p1Valid =
          !hasWallBetween(maze, current, p1) && !hasWallBetween(maze, p1, next);
        const p2Valid =
          !hasWallBetween(maze, current, p2) && !hasWallBetween(maze, p2, next);

        if (!p1Valid && p2Valid) {
          useP1 = false;
        }
      }

      if (useP1) {
        normalized.push(p1);
      } else {
        normalized.push(p2);
      }
    }
  }

  // Adicionar o último ponto.
  normalized.push(points[points.length - 1]);

  return normalized;
};
