import { describe, expect, it } from "vitest";
import { normalizePathToOrthogonal } from "../../components/maze/mazeUtils";
import type { Position } from "../../components/maze/types";

describe("normalizePathToOrthogonal", () => {
  it("retorna array vazio quando recebe array vazio", () => {
    expect(normalizePathToOrthogonal([])).toEqual([]);
  });

  it("retorna cópia do array quando recebe um único ponto", () => {
    const points: Position[] = [{ row: 0, col: 0 }];
    const result = normalizePathToOrthogonal(points);
    expect(result).toEqual([{ row: 0, col: 0 }]);
    // Deve ser uma cópia, não a mesma referência.
    expect(result).not.toBe(points);
  });

  it("não altera trajeto que já é ortogonal (apenas mudança em col)", () => {
    const points: Position[] = [
      { row: 0, col: 0 },
      { row: 0, col: 1 },
      { row: 0, col: 2 },
    ];
    expect(normalizePathToOrthogonal(points)).toEqual([
      { row: 0, col: 0 },
      { row: 0, col: 1 },
      { row: 0, col: 2 },
    ]);
  });

  it("não altera trajeto que já é ortogonal (apenas mudança em row)", () => {
    const points: Position[] = [
      { row: 0, col: 0 },
      { row: 1, col: 0 },
      { row: 2, col: 0 },
    ];
    expect(normalizePathToOrthogonal(points)).toEqual([
      { row: 0, col: 0 },
      { row: 1, col: 0 },
      { row: 2, col: 0 },
    ]);
  });

  it("não altera trajeto ortogonal misto (alternando row e col)", () => {
    const points: Position[] = [
      { row: 0, col: 0 },
      { row: 0, col: 1 },
      { row: 1, col: 1 },
      { row: 1, col: 2 },
    ];
    expect(normalizePathToOrthogonal(points)).toEqual([
      { row: 0, col: 0 },
      { row: 0, col: 1 },
      { row: 1, col: 1 },
      { row: 1, col: 2 },
    ]);
  });

  it("insere ponto intermediário para movimento diagonal simples", () => {
    const points: Position[] = [
      { row: 0, col: 0 },
      { row: 1, col: 1 },
    ];
    // Estratégia: move primeiro em col, depois em row.
    expect(normalizePathToOrthogonal(points)).toEqual([
      { row: 0, col: 0 },
      { row: 0, col: 1 }, // intermediário
      { row: 1, col: 1 },
    ]);
  });

  it("insere ponto intermediário para salto grande diagonal", () => {
    // Exemplo da HU: (1,1) → (3,2)
    // Em Position: row=1,col=1 → row=2,col=3
    const points: Position[] = [
      { row: 1, col: 1 },
      { row: 2, col: 3 },
    ];
    expect(normalizePathToOrthogonal(points)).toEqual([
      { row: 1, col: 1 },
      { row: 1, col: 3 }, // intermediário: move col primeiro
      { row: 2, col: 3 },
    ]);
  });

  it("trata múltiplas diagonais consecutivas", () => {
    const points: Position[] = [
      { row: 0, col: 0 },
      { row: 1, col: 1 },
      { row: 2, col: 2 },
      { row: 3, col: 3 },
    ];
    expect(normalizePathToOrthogonal(points)).toEqual([
      { row: 0, col: 0 },
      { row: 0, col: 1 }, // intermediário
      { row: 1, col: 1 },
      { row: 1, col: 2 }, // intermediário
      { row: 2, col: 2 },
      { row: 2, col: 3 }, // intermediário
      { row: 3, col: 3 },
    ]);
  });

  it("trata mix de movimentos ortogonais e diagonais", () => {
    const points: Position[] = [
      { row: 0, col: 0 },
      { row: 0, col: 1 }, // ortogonal
      { row: 1, col: 2 }, // diagonal
      { row: 2, col: 2 }, // ortogonal
      { row: 3, col: 4 }, // diagonal
    ];
    expect(normalizePathToOrthogonal(points)).toEqual([
      { row: 0, col: 0 },
      { row: 0, col: 1 },
      { row: 0, col: 2 }, // intermediário para (1,2)
      { row: 1, col: 2 },
      { row: 2, col: 2 },
      { row: 2, col: 4 }, // intermediário para (3,4)
      { row: 3, col: 4 },
    ]);
  });

  it("trata movimento diagonal inverso (decremento em ambos eixos)", () => {
    const points: Position[] = [
      { row: 3, col: 3 },
      { row: 1, col: 1 },
    ];
    expect(normalizePathToOrthogonal(points)).toEqual([
      { row: 3, col: 3 },
      { row: 3, col: 1 }, // intermediário
      { row: 1, col: 1 },
    ]);
  });

  it("preserva pontos duplicados consecutivos sem alterar", () => {
    const points: Position[] = [
      { row: 0, col: 0 },
      { row: 0, col: 0 },
      { row: 1, col: 1 },
    ];
    expect(normalizePathToOrthogonal(points)).toEqual([
      { row: 0, col: 0 },
      { row: 0, col: 0 },
      { row: 0, col: 1 }, // intermediário
      { row: 1, col: 1 },
    ]);
  });
});
