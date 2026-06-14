import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";

import { EstadosDashboard } from "../../components/estados/EstadosDashboard";
import type { UseTelemetriaReturn } from "../../hooks/useTelemetria";
import {
  mockConcluida,
  mockEmAndamento,
  mockFalha,
} from "../utils/telemetria-mocks";

function criarTelemetria(
  indicadores: UseTelemetriaReturn["indicadores"],
): UseTelemetriaReturn {
  return {
    indicadores,
    configSessao: { dimensao: 16 },
    statusConexao: "online",
    mensagemStatusConexao: null,
    enviarPacote: () => undefined,
    conectado: true,
    erro: null,
    ultimaMovimentacao: null,
    filaMovimentacoes: [],
    limparFilaMovimentacoes: () => undefined,
    alertaSemSinal: false,
    contadorNovoRecorde: 0,
  };
}

describe("EstadosDashboard", () => {
  it("atualiza de 'Desafio em andamento' para 'Desafio cumprido'", () => {
    const { rerender } = render(
      <EstadosDashboard telemetria={criarTelemetria(mockEmAndamento)} />,
    );

    expect(
      screen.getByRole("heading", { name: "Desafio em andamento" }),
    ).toBeInTheDocument();

    rerender(
      <EstadosDashboard telemetria={criarTelemetria(mockConcluida)} />,
    );

    const heading = screen.getByRole("heading", { name: "Desafio cumprido" });
    const card = heading.closest("section");

    expect(heading).toBeInTheDocument();
    expect(card).not.toBeNull();
    expect(card).toHaveClass("bg-emerald-500/10", "text-emerald-400");
    expect(screen.getByText("Micromouse alcançou a área central")).toBeInTheDocument();
  });

  it("atualiza de 'Desafio em andamento' para 'Desafio não cumprido'", () => {
    const { rerender } = render(
      <EstadosDashboard telemetria={criarTelemetria(mockEmAndamento)} />,
    );

    expect(
      screen.getByRole("heading", { name: "Desafio em andamento" }),
    ).toBeInTheDocument();

    rerender(
      <EstadosDashboard telemetria={criarTelemetria(mockFalha)} />,
    );

    const heading = screen.getByRole("heading", { name: "Desafio não cumprido" });
    const card = heading.closest("section");

    expect(heading).toBeInTheDocument();
    expect(card).not.toBeNull();
    expect(card).toHaveClass("bg-rose-500/10", "text-rose-400");
    expect(screen.getByText("Sessão encerrada sem atingir o objetivo")).toBeInTheDocument();
  });
});
