import type { Direction, TelemetryUpdate } from "./types";

type MazeSize = 4 | 8 | 16;

type MockPacketKind = "inicial" | "movimentacao" | "rota" | "final";

export interface MockTelemetryPacket {
  kind: MockPacketKind;
  payload: Record<string, unknown>;
}

// Parametros para simular telemetria em tempo real.
interface MockTelemetryOptions {
  size: MazeSize;
  intervalMs: number;
  maxSteps: number;
  onTelemetry: (update: TelemetryUpdate) => void;
  onPacket?: (packet: MockTelemetryPacket) => void;
  onFinish: () => void;
}

const buildSnakePath = (size: MazeSize, desiredPoints: number) => {
  const path = [{ x: 0, y: 0 }];
  let horizontalDirection: "east" | "west" = "east";

  while (path.length < desiredPoints) {
    const current = path[path.length - 1];
    const nextX =
      horizontalDirection === "east" ? current.x + 1 : current.x - 1;

    if (nextX >= 0 && nextX < size) {
      path.push({ x: nextX, y: current.y });
      continue;
    }

    const nextY = current.y + 1;
    if (nextY >= size) {
      break;
    }

    path.push({ x: current.x, y: nextY });
    horizontalDirection = horizontalDirection === "east" ? "west" : "east";
  }

  return path;
};

const encodeWalls = (size: MazeSize, x: number, y: number) => {
  let mask = 0;

  if (y === size - 1) {
    mask |= 1;
  }
  if (y === 0) {
    mask |= 2;
  }
  if (x === size - 1) {
    mask |= 4;
  }
  if (x === 0) {
    mask |= 8;
  }

  if ((x + y) % 3 === 0) {
    mask |= 1;
  }
  if (size >= 8 && x % 4 === 1 && y % 2 === 0) {
    mask |= 4;
  }

  return mask;
};

const inferDirection = (
  current: { x: number; y: number },
  next: { x: number; y: number },
): Direction => {
  if (next.x > current.x) {
    return "east";
  }
  if (next.x < current.x) {
    return "west";
  }
  if (next.y > current.y) {
    return "south";
  }
  return "north";
};

const toTelemetryPosition = (size: number, x: number, y: number) => ({
  row: size - 1 - y,
  col: x,
});

const buildMockPackets = (size: MazeSize): MockTelemetryPacket[] => {
  const pointTarget = size === 4 ? 7 : size === 8 ? 11 : 17;
  const path = buildSnakePath(size, pointTarget);
  const movementPackets = path.slice(1).map((point, index) => ({
    kind: "movimentacao" as const,
    payload: {
      id_corrida: size,
      timestamp_ms: (index + 1) * 750,
      x: point.x,
      y: point.y,
      w: encodeWalls(size, point.x, point.y),
    },
  }));

  return [
    {
      kind: "inicial",
      payload: {
        id_corrida: size,
        timestamp_ms: 0,
        dimensao: size,
        tentativa: 1,
        bateria: 100,
      },
    },
    {
      kind: "rota",
      payload: {
        id_corrida: size,
        timestamp_ms: 500,
        rota: path.map((point) => [point.x, point.y]),
      },
    },
    ...movementPackets,
    {
      kind: "final",
      payload: {
        id_corrida: size,
        timestamp_ms: (path.length + 1) * 750,
        sucesso: true,
        v_med: size === 4 ? 0.22 : size === 8 ? 0.28 : 0.34,
        bateria: size === 4 ? 92 : size === 8 ? 90 : 88,
      },
    },
  ];
};

export const MOCK_TELEMETRY_BY_SIZE: Record<MazeSize, MockTelemetryPacket[]> = {
  4: buildMockPackets(4),
  8: buildMockPackets(8),
  16: buildMockPackets(16),
};

const packetToTelemetryUpdate = (
  packet: MockTelemetryPacket,
  size: MazeSize,
  previous: { x: number; y: number } | null,
): TelemetryUpdate | null => {
  if (packet.kind !== "movimentacao") {
    return null;
  }

  const x = Number(packet.payload.x);
  const y = Number(packet.payload.y);
  if (!Number.isFinite(x) || !Number.isFinite(y)) {
    return null;
  }

  const current = { x, y };
  const origin = previous ?? { x: 0, y: 0 };

  return {
    position: toTelemetryPosition(size, current.x, current.y),
    direction: inferDirection(origin, current),
    moved: true,
    hitWall: false,
  };
};

// Fabrica o simulador com ciclo de atualizacoes periodicas.
export const createMockTelemetry = (options: MockTelemetryOptions) => {
  let timer: number | null = null;
  let packetIndex = 0;
  let previousPosition: { x: number; y: number } | null = null;
  const packets = MOCK_TELEMETRY_BY_SIZE[options.size];

  const tick = () => {
    if (packetIndex >= packets.length || packetIndex >= options.maxSteps) {
      options.onFinish();
      return;
    }

    const packet = packets[packetIndex];
    packetIndex += 1;
    options.onPacket?.(packet);

    if (packet.kind === "final") {
      options.onFinish();
      return;
    }

    const nextTelemetry = packetToTelemetryUpdate(
      packet,
      options.size,
      previousPosition,
    );

    if (nextTelemetry) {
      options.onTelemetry(nextTelemetry);
      previousPosition = {
        x: nextTelemetry.position.col,
        y: options.size - 1 - nextTelemetry.position.row,
      };
    }
  };

  return {
    start: () => {
      if (timer !== null) {
        return;
      }
      timer = window.setInterval(tick, options.intervalMs);
    },
    stop: () => {
      if (timer === null) {
        return;
      }
      window.clearInterval(timer);
      timer = null;
    },
    reset: () => {
      packetIndex = 0;
      previousPosition = null;
    },
  };
};
