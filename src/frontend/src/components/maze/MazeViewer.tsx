import { useEffect, useRef, useState } from "react";
import { useTelemetria } from "../../hooks/useTelemetria";
import { createMaze, isInsideMaze, markVisited, markWall, normalizePathToOrthogonal, hasWallBetween } from "./mazeUtils";
import type { Cell, Direction, Position } from "./types";

const DEFAULT_GRID_SIZE = 8;
const GRID_SIZES = [16, 8, 4] as const;

const positionsEqual = (a: Position, b: Position) =>
  a.row === b.row && a.col === b.col;

export default function MazeViewer() {
  const { filaMovimentacoes, limparFilaMovimentacoes, configSessao, indicadores, statusConexao } =
    useTelemetria();
  // Estado principal da corrida e do labirinto.
  const [gridSize, setGridSize] = useState(DEFAULT_GRID_SIZE);
  const [maze, setMaze] = useState(() => createMaze(DEFAULT_GRID_SIZE));
  const [position, setPosition] = useState<Position>({ row: 0, col: 0 });
  const [direction, setDirection] = useState<Direction>("east");
  const [path, setPath] = useState<Position[]>([]);
  const [viewMode, setViewMode] = useState<"live" | "history">("live");
  const [isHistoryOpen, setIsHistoryOpen] = useState(false);
  const [history, setHistory] = useState<
    {
      maze: Cell[][];
      path: Position[];
      endPosition: Position;
      endDirection: Direction;
      gridSize: number;
    }[]
  >([]);
  const [historyIndex, setHistoryIndex] = useState(0);
  const [sessionStatus, setSessionStatus] = useState<
    "idle" | "running" | "finished"
  >("idle");
  const stepRef = useRef(0);
  const sessionIdRef = useRef<number | null>(null);
  // Mantem posicao atual sem depender do fechamento do useEffect.
  const positionRef = useRef<Position>({ row: 0, col: 0 });
  const mazeRef = useRef<Cell[][]>(maze);
  const pathRef = useRef<Position[]>(path);
  const directionRef = useRef<Direction>(direction);
  const snapshot = viewMode === "history" ? history[historyIndex] : undefined;
  const displayMaze = snapshot ? snapshot.maze : maze;
  const displayPath = snapshot ? snapshot.path : path;
  const displayPosition = snapshot ? snapshot.endPosition : position;
  const displayGridSize = snapshot ? snapshot.gridSize : gridSize;
  const displayGridDimension =
    displayGridSize === 16
      ? "min(72vmin, 640px)"
      : displayGridSize === 8
        ? "min(70vmin, 520px)"
        : "min(60vmin, 360px)";
  const wallShadowColor = "rgb(9 9 11)";
  const origin = { row: 0, col: 0 };
  const rawPathPoints = [origin, ...displayPath];
  if (!positionsEqual(rawPathPoints[rawPathPoints.length - 1], displayPosition)) {
    rawPathPoints.push(displayPosition);
  }
  // Normaliza o trajeto para evitar linhas diagonais entre células.
  const pathPoints = normalizePathToOrthogonal(rawPathPoints, displayMaze);
  const pathPointsString = pathPoints
    .map((point) => `${point.col + 0.5},${point.row + 0.5}`)
    .join(" ");

  const cloneMaze = (source: Cell[][]) =>
    source.map((row) =>
      row.map((cell) => ({ ...cell, walls: { ...cell.walls } })),
    );

  const resetRunState = (size: number) => {
    stepRef.current = 0;
    sessionIdRef.current = null;
    setSessionStatus("idle");
    setPosition({ row: 0, col: 0 });
    positionRef.current = { row: 0, col: 0 };
    setDirection("east");
    setPath([]);
    setMaze(createMaze(size));
    setViewMode("live");
    setIsHistoryOpen(false);
  };

  const updateGridSize = (size: number) => {
    if (size === gridSize) {
      return;
    }

    resetRunState(size);
    setGridSize(size);
  };

  const startSession = () => {
    resetRunState(gridSize);
  };

  const openHistory = () => {
    if (history.length === 0) {
      return;
    }
    setSessionStatus("finished");
    setViewMode("history");
    setHistoryIndex(0);
    setIsHistoryOpen(true);
  };

  const selectHistory = (index: number) => {
    setHistoryIndex(index);
    setViewMode("history");
    setIsHistoryOpen(false);
  };

  useEffect(() => {
    mazeRef.current = maze;
  }, [maze]);

  useEffect(() => {
    pathRef.current = path;
  }, [path]);

  useEffect(() => {
    directionRef.current = direction;
  }, [direction]);

  useEffect(() => {
    const dimensao = Number(configSessao.dimensao);
    if (
      !Number.isNaN(dimensao) &&
      (dimensao === 4 || dimensao === 8 || dimensao === 16)
    ) {
      if (dimensao !== gridSize) {
        resetRunState(dimensao);
        setGridSize(dimensao);
      }
    }
  }, [configSessao.dimensao, gridSize]);

  useEffect(() => {
    if (filaMovimentacoes.length === 0) {
      return;
    }

    let nextMaze = mazeRef.current;
    let nextPath = [...pathRef.current];
    let nextPosition = positionRef.current;
    let nextDirection = directionRef.current;
    let statusChanged = false;

    for (const mov of filaMovimentacoes) {
      if (sessionIdRef.current !== mov.id_corrida) {
        sessionIdRef.current = mov.id_corrida;
        // Reinicia variaveis locais (equivalente ao resetRunState, para não agendar varios renders)
        stepRef.current = 0;
        setSessionStatus("idle");
        nextMaze = createMaze(gridSize);
        nextPath = [];
        nextPosition = { row: 0, col: 0 };
        nextDirection = "east";
        setViewMode("live");
        setIsHistoryOpen(false);
      }

      const currentTarget = {
        row: mov.y,
        col: mov.x,
      };

      if (!isInsideMaze(currentTarget, gridSize)) {
        continue;
      }

      if (currentTarget.row === nextPosition.row - 1) {
        nextDirection = "north";
      } else if (currentTarget.row === nextPosition.row + 1) {
        nextDirection = "south";
      } else if (currentTarget.col === nextPosition.col + 1) {
        nextDirection = "east";
      } else if (currentTarget.col === nextPosition.col - 1) {
        nextDirection = "west";
      }

      if (mov.paredes.norte) nextMaze = markWall(nextMaze, currentTarget, "north");
      if (mov.paredes.sul) nextMaze = markWall(nextMaze, currentTarget, "south");
      if (mov.paredes.leste) nextMaze = markWall(nextMaze, currentTarget, "east");
      if (mov.paredes.oeste) nextMaze = markWall(nextMaze, currentTarget, "west");

      stepRef.current += 1;
      nextMaze = markVisited(nextMaze, currentTarget, stepRef.current);

      const last = nextPath.length > 0 ? nextPath[nextPath.length - 1] : { row: 0, col: 0 };
      if (!positionsEqual(last, currentTarget)) {
        const dx = Math.abs(currentTarget.col - last.col);
        const dy = Math.abs(currentTarget.row - last.row);

        if (dx + dy === 1) {
          if (!hasWallBetween(nextMaze, last, currentTarget)) {
            nextPath.push(currentTarget);
          } else {
            console.warn("Movimento ignorado: parede bloqueando caminho", last, currentTarget);
          }
        } else {
          const p1 = { row: last.row, col: currentTarget.col };
          const p2 = { row: currentTarget.row, col: last.col };

          const p1Valid = !hasWallBetween(nextMaze, last, p1) && !hasWallBetween(nextMaze, p1, currentTarget);
          const p2Valid = !hasWallBetween(nextMaze, last, p2) && !hasWallBetween(nextMaze, p2, currentTarget);

          if (p1Valid) {
            nextPath.push(p1, currentTarget);
          } else if (p2Valid) {
            nextPath.push(p2, currentTarget);
          } else {
            console.warn("Movimento diagonal inválido: bloqueado por paredes", last, currentTarget);
          }
        }
      }

      nextPosition = currentTarget;
      statusChanged = true;
    }

    if (statusChanged) {
      setMaze(nextMaze);
      setPath(nextPath);
      setPosition(nextPosition);
      positionRef.current = nextPosition;
      setDirection(nextDirection);
      setSessionStatus("running");
    }

    limparFilaMovimentacoes();
  }, [filaMovimentacoes, gridSize, limparFilaMovimentacoes]);

  useEffect(() => {
    if (
      indicadores.status_corrida !== "concluida" &&
      indicadores.status_corrida !== "falha"
    ) {
      return;
    }

    setHistory((prev) => [
      {
        maze: cloneMaze(mazeRef.current),
        path: [...pathRef.current],
        endPosition: positionRef.current,
        endDirection: directionRef.current,
        gridSize,
      },
      ...prev,
    ]);
    setHistoryIndex(0);
    setSessionStatus("finished");
  }, [indicadores.status_corrida, gridSize]);

  return (
    <div className="rounded-2xl border border-zinc-200 bg-white p-6 shadow-sm">
      <header className="flex flex-col gap-4 border-b border-zinc-100 pb-4 md:flex-row md:items-center md:justify-between">
        <div>
          <p className="text-xs font-semibold uppercase tracking-wide text-zinc-500">
            Labirinto
          </p>
          <h2 className="text-xl font-semibold text-zinc-950">
            Mapa de navegacao em tempo real
          </h2>
        </div>
        <div className="flex flex-wrap gap-2">
          <button
            type="button"
            className="rounded-lg bg-zinc-950 px-3 py-2 text-xs font-semibold text-white transition hover:bg-zinc-800"
            onClick={startSession}
          >
            Limpar mapa
          </button>
          <button
            type="button"
            className="rounded-lg border border-zinc-200 bg-white px-3 py-2 text-xs font-semibold text-zinc-700 transition hover:bg-zinc-50"
            onClick={openHistory}
          >
            Historico
          </button>
          {GRID_SIZES.map((size) => (
            <button
              key={size}
              type="button"
              className="rounded-lg border border-zinc-200 bg-white px-3 py-2 text-xs font-semibold text-zinc-700 transition hover:bg-zinc-50"
              onClick={() => updateGridSize(size)}
            >
              {size}x{size}
            </button>
          ))}
        </div>
      </header>

      <div className="mt-6 flex flex-col gap-6 lg:flex-row">
        <div className="flex-1">
          <div className="relative rounded-2xl border border-zinc-200 bg-zinc-50 p-3">
            <div
              className="relative grid"
              style={{
                gridTemplateColumns: `repeat(${displayGridSize}, minmax(0, 1fr))`,
                width: displayGridDimension,
                height: displayGridDimension,
              }}
            >
              {displayMaze.map((row, rowIndex) =>
                row.map((cell, colIndex) => {
                  const cellPosition = { row: rowIndex, col: colIndex };
                  const isCurrent = positionsEqual(
                    cellPosition,
                    displayPosition,
                  );
                  const isOnPath = displayPath.some((step) =>
                    positionsEqual(step, cellPosition),
                  );
                  const backgroundColor = isCurrent
                    ? "rgb(125 211 252)"
                    : cell.visited
                      ? "rgb(186 230 253)"
                      : isOnPath
                        ? "rgb(224 242 254)"
                        : "rgb(255 255 255)";
                  const wallShadows = [
                    cell.walls.north
                      ? `inset 0 2px 0 0 ${wallShadowColor}`
                      : null,
                    cell.walls.south
                      ? `inset 0 -2px 0 0 ${wallShadowColor}`
                      : null,
                    cell.walls.east
                      ? `inset -2px 0 0 0 ${wallShadowColor}`
                      : null,
                    cell.walls.west
                      ? `inset 2px 0 0 0 ${wallShadowColor}`
                      : null,
                  ]
                    .filter(Boolean)
                    .join(", ");
                  const classes = [
                    "relative aspect-square border border-zinc-200",
                  ]
                    .filter(Boolean)
                    .join(" ");

                  return (
                    <div
                      key={`${rowIndex}-${colIndex}`}
                      className={`z-20 ${classes}`}
                      style={{
                        backgroundColor,
                        boxShadow: wallShadows || undefined,
                      }}
                    />
                  );
                }),
              )}
              <svg
                className="pointer-events-none absolute inset-0 z-20 h-full w-full"
                viewBox={`0 0 ${displayGridSize} ${displayGridSize}`}
                aria-hidden="true"
              >
                <polyline
                  points={pathPointsString}
                  fill="none"
                  stroke="rgb(37 99 235)"
                  strokeWidth="0.04"
                  strokeLinecap="round"
                  strokeLinejoin="round"
                />
              </svg>
              <div
                className="pointer-events-none absolute z-30 h-3 w-3 rounded-full bg-black"
                style={{
                  left: `${((displayPosition.col + 0.5) / displayGridSize) * 100}%`,
                  top: `${((displayPosition.row + 0.5) / displayGridSize) * 100}%`,
                  transform: "translate(-50%, -50%)",
                }}
              />
            </div>
          </div>
        </div>

        <aside className="w-full lg:w-72">
          <div className="rounded-2xl border border-zinc-200 bg-white p-4 shadow-sm">
            <p className="text-xs font-semibold uppercase tracking-wide text-zinc-500">
              Status da corrida
            </p>
            <div className="mt-3 space-y-2 text-sm text-zinc-700">
              <p className="flex items-center justify-between">
                <span>Sessao</span>
                <span className="font-semibold text-zinc-900">
                  {sessionStatus}
                </span>
              </p>
              <p className="flex items-center justify-between">
                <span>Conexao</span>
                <span className="font-semibold text-zinc-900">
                  {statusConexao}
                </span>
              </p>
              <p className="flex items-center justify-between">
                <span>Posicao</span>
                <span className="font-semibold text-zinc-900">
                  ({displayPosition.row}, {displayPosition.col})
                </span>
              </p>
              <p className="flex items-center justify-between">
                <span>Grade</span>
                <span className="font-semibold text-zinc-900">
                  {displayGridSize}x{displayGridSize}
                </span>
              </p>
            </div>
          </div>
        </aside>
      </div>

      {isHistoryOpen && (
        <div
          className="fixed inset-0 z-50 flex items-center justify-center bg-zinc-900/40 p-4"
          role="dialog"
          aria-modal="true"
        >
          <div className="w-full max-w-lg rounded-2xl bg-white p-6 shadow-xl">
            <div className="flex items-center justify-between border-b border-zinc-100 pb-3">
              <h2 className="text-lg font-semibold text-zinc-900">
                Historico de Corridas
              </h2>
              <button
                type="button"
                className="rounded-lg border border-zinc-200 px-3 py-1.5 text-sm text-zinc-600 transition hover:bg-zinc-50"
                onClick={() => setIsHistoryOpen(false)}
              >
                Fechar
              </button>
            </div>
            <div className="mt-4 space-y-2">
              {history.map((run, index) => (
                <button
                  key={`history-${index}`}
                  type="button"
                  className="flex w-full items-center justify-between rounded-xl border border-zinc-200 px-4 py-3 text-left text-sm text-zinc-700 transition hover:border-zinc-300 hover:bg-zinc-50"
                  onClick={() => selectHistory(index)}
                >
                  <div>
                    <span className="block text-sm font-semibold text-zinc-900">
                      Corrida {index + 1}
                    </span>
                    <span className="text-xs text-zinc-500">
                      {run.gridSize}x{run.gridSize} · {run.path.length} passos
                    </span>
                  </div>
                  <span className="text-lg text-zinc-400">→</span>
                </button>
              ))}
              {history.length === 0 && (
                <p className="rounded-xl border border-dashed border-zinc-200 px-4 py-6 text-center text-sm text-zinc-500">
                  Nenhuma corrida registrada ainda.
                </p>
              )}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
