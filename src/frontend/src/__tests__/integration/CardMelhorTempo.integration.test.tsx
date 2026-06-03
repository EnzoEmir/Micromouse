import { render, screen, act, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { CardMelhorTempo } from "../../components/CardMelhorTempo";
import type { MelhorTempoResponse } from "../../types/corrida";

// ---------------------------------------------------------------------------
// Mock do service (camada de integração real: hook → service mockado)
// ---------------------------------------------------------------------------

vi.mock("../../services/corrida", () => ({
  fetchMelhorTempo: vi.fn(),
  listarCorridasResumo: vi.fn(),
  obterCorrida: vi.fn(),
}));

import { fetchMelhorTempo } from "../../services/corrida";
const mockFetchMelhorTempo = vi.mocked(fetchMelhorTempo);

// ---------------------------------------------------------------------------
// Dados de teste
// ---------------------------------------------------------------------------

const mockRecorde: MelhorTempoResponse = {
  id_corrida: 11,
  tempo_total: 93210,
  data_hora_fim: "2026-05-01T14:30:00",
  tipo_labirinto: "4X4",
};

const mockNovoRecorde: MelhorTempoResponse = {
  id_corrida: 15,
  tempo_total: 75000,
  data_hora_fim: "2026-06-01T10:00:00",
  tipo_labirinto: "4X4",
};

// ---------------------------------------------------------------------------
// CT-CMT-INT-01 — Integração hook + service: estado vazio (CA-17-03)
// ---------------------------------------------------------------------------

describe("CT-CMT-INT-01 — Integração: estado vazio via service (CA-17-03)", () => {
  beforeEach(() => {
    mockFetchMelhorTempo.mockResolvedValue(null);
  });

  afterEach(() => {
    vi.clearAllMocks();
  });

  it("exibe 'Nenhum desafio concluído ainda' quando service retorna null", async () => {
    render(<CardMelhorTempo tipo="4X4" />);

    await waitFor(() => {
      expect(
        screen.getByText("Nenhum desafio concluído ainda"),
      ).toBeInTheDocument();
    });
  });

  it("chama fetchMelhorTempo com o tipo correto", async () => {
    render(<CardMelhorTempo tipo="8X8" />);

    await waitFor(() => {
      expect(mockFetchMelhorTempo).toHaveBeenCalledWith("8X8");
    });
  });
});

// ---------------------------------------------------------------------------
// CT-CMT-INT-02 — Integração hook + service: com recorde (CA-17-01, CA-17-04)
// ---------------------------------------------------------------------------

describe("CT-CMT-INT-02 — Integração: exibe recorde via service (CA-17-01, CA-17-04)", () => {
  beforeEach(() => {
    mockFetchMelhorTempo.mockResolvedValue(mockRecorde);
  });

  afterEach(() => {
    vi.clearAllMocks();
  });

  it("exibe o tempo formatado após resolução do service", async () => {
    render(<CardMelhorTempo tipo="4X4" />);

    await waitFor(() => {
      expect(screen.getByText("01:33.210")).toBeInTheDocument();
    });
  });

  it("exibe o id_corrida formatado após resolução do service", async () => {
    render(<CardMelhorTempo tipo="4X4" />);

    await waitFor(() => {
      expect(screen.getByText(/^#\d{4}-\d{2}-\d{2}-011$/)).toBeInTheDocument();
    });
  });

  it("exibe a data formatada após resolução do service", async () => {
    render(<CardMelhorTempo tipo="4X4" />);

    await waitFor(() => {
      expect(screen.getByText(/01\/05\/2026/)).toBeInTheDocument();
    });
  });

  it("exibe loading antes de resolver e recorde depois", async () => {
    let resolver!: (value: MelhorTempoResponse) => void;
    mockFetchMelhorTempo.mockReturnValue(
      new Promise((res) => { resolver = res; }),
    );

    render(<CardMelhorTempo tipo="4X4" />);

    // loading: valores "--"
    expect(screen.getAllByText("--").length).toBeGreaterThanOrEqual(2);

    // resolve o service
    await act(async () => {
      resolver(mockRecorde);
    });

    await waitFor(() => {
      expect(screen.getByText("01:33.210")).toBeInTheDocument();
    });
  });
});

// ---------------------------------------------------------------------------
// CT-CMT-INT-03 — Integração: refetch atualiza o card (CA-17-02)
// ---------------------------------------------------------------------------

describe("CT-CMT-INT-03 — Integração: refetch atualiza valores do card (CA-17-02)", () => {
  afterEach(() => {
    vi.clearAllMocks();
  });

  it("atualiza o card quando tipo muda (simula novo recorde via prop)", async () => {
    mockFetchMelhorTempo.mockResolvedValue(mockRecorde);

    const { rerender } = render(<CardMelhorTempo tipo="4X4" />);

    await waitFor(() => {
      expect(screen.getByText("01:33.210")).toBeInTheDocument();
    });

    // Simula novo recorde para tipo diferente
    mockFetchMelhorTempo.mockResolvedValue(mockNovoRecorde);

    await act(async () => {
      rerender(<CardMelhorTempo tipo="8X8" />);
    });

    await waitFor(() => {
      // 75000ms = 01:15.000
      expect(screen.getByText("01:15.000")).toBeInTheDocument();
    });
  });

  it("exibe novo recorde via props controladas após refetch", async () => {
    const { rerender } = render(
      <CardMelhorTempo tipo="4X4" melhorTempo={mockRecorde} loading={false} erro={null} />,
    );

    expect(screen.getByText("01:33.210")).toBeInTheDocument();

    // Simula atualização após refetch (CA-17-02)
    await act(async () => {
      rerender(
        <CardMelhorTempo
          tipo="4X4"
          melhorTempo={mockNovoRecorde}
          loading={false}
          erro={null}
        />,
      );
    });

    expect(screen.getByText("01:15.000")).toBeInTheDocument();
  });

  it("exibe loading durante refetch e novo recorde depois (CA-17-02)", async () => {
    const { rerender } = render(
      <CardMelhorTempo tipo="4X4" melhorTempo={mockRecorde} loading={false} erro={null} />,
    );

    expect(screen.getByText("01:33.210")).toBeInTheDocument();

    // Simula loading durante refetch
    await act(async () => {
      rerender(
        <CardMelhorTempo tipo="4X4" melhorTempo={null} loading={true} erro={null} />,
      );
    });

    expect(screen.getAllByText("--").length).toBeGreaterThanOrEqual(2);

    // Resolve com novo recorde
    await act(async () => {
      rerender(
        <CardMelhorTempo
          tipo="4X4"
          melhorTempo={mockNovoRecorde}
          loading={false}
          erro={null}
        />,
      );
    });

    expect(screen.getByText("01:15.000")).toBeInTheDocument();
  });
});

// ---------------------------------------------------------------------------
// CT-CMT-INT-04 — Integração: tratamento de erro do service
// ---------------------------------------------------------------------------

describe("CT-CMT-INT-04 — Integração: erro do service exibe mensagem", () => {
  afterEach(() => {
    vi.clearAllMocks();
  });

  it("exibe mensagem de erro quando service lança exceção", async () => {
    mockFetchMelhorTempo.mockRejectedValue(new Error("Falha na requisição"));

    render(<CardMelhorTempo tipo="4X4" />);

    await waitFor(() => {
      expect(screen.getByText(/Não foi possível carregar/i)).toBeInTheDocument();
      expect(screen.getByText(/Falha na requisição/i)).toBeInTheDocument();
    });
  });
});