import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { CorridasTable } from "../../components/corrida/CorridaTable";

describe("CorridasTable", () => {
  const mockCorridas = [
    {
      id_corrida: 1,
      data_hora_inicio: "2026-05-02T10:00:00Z",
      tempo_total: 120,
      status_corrida: "CONCLUIDA" as const,
      velocidade_media: 25.5,
      tipo_labirinto: "16X16" as const,
    },
    {
      id_corrida: 2,
      data_hora_inicio: "2026-05-02T11:00:00Z",
      tempo_total: null,
      status_corrida: "ABORTADA" as const,
      velocidade_media: null,
      tipo_labirinto: "4X4" as const,
    },
    {
      id_corrida: 3,
      data_hora_inicio: null,
      tempo_total: null,
      status_corrida: "EM_ANDAMENTO" as const,
      velocidade_media: null,
      tipo_labirinto: null,
    }
  ];

  it("renderiza estado de loading", () => {
    render(<CorridasTable corridas={[]} carregando={true} mensagemVazio={null} onSelecionar={vi.fn()} />);
    expect(screen.getByText("Carregando corridas...")).toBeInTheDocument();
  });

  it("renderiza mensagem de vazio", () => {
    render(<CorridasTable corridas={[]} carregando={false} mensagemVazio="Nenhuma corrida encontrada" onSelecionar={vi.fn()} />);
    expect(screen.getByText("Nenhuma corrida encontrada")).toBeInTheDocument();
  });

  it("renderiza lista de corridas", () => {
    render(<CorridasTable corridas={mockCorridas} carregando={false} mensagemVazio={null} onSelecionar={vi.fn()} />);
    
    // Labels
    expect(screen.getByText("Concluída")).toBeInTheDocument();
    expect(screen.getByText("Abortada")).toBeInTheDocument();
    expect(screen.getByText("Em andamento")).toBeInTheDocument();

    expect(screen.getByText("16X16")).toBeInTheDocument();
    expect(screen.getByText("4X4")).toBeInTheDocument();
    
    // Tempo
    expect(screen.getByText("120")).toBeInTheDocument();

    // Velocidade
    expect(screen.getByText("25.50 cm/s")).toBeInTheDocument();
  });

  it("chama onSelecionar ao clicar na linha", () => {
    const onSelecionarMock = vi.fn();
    render(<CorridasTable corridas={mockCorridas} carregando={false} mensagemVazio={null} onSelecionar={onSelecionarMock} />);
    
    const row = screen.getByText("16X16");
    fireEvent.click(row);
    
    expect(onSelecionarMock).toHaveBeenCalledWith(1);
  });
});
