import { render, screen } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { CorridaDetailPanel } from "../../components/corrida/CorridaDetailPanel";

describe("CorridaDetailPanel", () => {
  it("renderiza estado vazio quando corrida é null", () => {
    render(<CorridaDetailPanel corrida={null} carregando={false} />);
    expect(screen.getByText("Selecione uma corrida na lista para ver o detalhe completo e o percurso.")).toBeInTheDocument();
  });

  it("renderiza estado de loading", () => {
    render(<CorridaDetailPanel corrida={{} as any} carregando={true} />);
    expect(screen.getByText("Carregando detalhe...")).toBeInTheDocument();
  });

  it("renderiza detalhes da corrida", () => {
    const mockCorrida = {
      id_corrida: 123,
      tempo_total: 45.6,
      tensao_media: null,
      corrente_media: null,
      velocidade_maxima_percurso: null,
      velocidade_media: 30.123,
      status_corrida: "CONCLUIDA" as const,
      desafio_cumprido: true,
      data_hora_inicio: "2026-05-02T10:00:00Z",
      data_hora_fim: "2026-05-02T10:01:00Z",
      tipo_labirinto: "8X8" as const,
      percurso: [
        {
          id_percurso: 1,
          id_celula: 10,
          data_hora_passagem: "2026-05-02T10:00:05Z",
          tipo_percurso: "EXPLORACAO"
        }
      ]
    };

    render(<CorridaDetailPanel corrida={mockCorrida} carregando={false} />);
    
    expect(screen.getByText("#123")).toBeInTheDocument();
    expect(screen.getByText("8X8")).toBeInTheDocument();
    expect(screen.getByText("45.6")).toBeInTheDocument();
    expect(screen.getByText("30.12 cm/s")).toBeInTheDocument();
    expect(screen.getByText("Sim")).toBeInTheDocument();
    expect(screen.getByText("1 passos")).toBeInTheDocument();

    expect(screen.getByText("EXPLORACAO")).toBeInTheDocument();
    expect(screen.getByText("#1")).toBeInTheDocument();
    expect(screen.getByText("Célula: 10")).toBeInTheDocument();
  });

  it("lida com desafio_cumprido null ou false e dados vazios", () => {
    const mockCorrida = {
      id_corrida: 124,
      tempo_total: null,
      tensao_media: null,
      corrente_media: null,
      velocidade_maxima_percurso: null,
      velocidade_media: null,
      status_corrida: "ABORTADA" as const,
      desafio_cumprido: null,
      data_hora_inicio: null,
      data_hora_fim: null,
      tipo_labirinto: null,
      percurso: []
    };

    render(<CorridaDetailPanel corrida={mockCorrida} carregando={false} />);
    
    const fallbackTexts = screen.getAllByText("--");
    expect(fallbackTexts.length).toBeGreaterThan(0);
  });
});
