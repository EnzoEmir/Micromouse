import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";

import { EstadoAtualCard } from "../../components/estados/EstadoAtualCard";

describe("EstadoAtualCard", () => {
  it.each([
    {
      estado: "em_andamento" as const,
      rotulo: "Desafio em andamento",
      severidade: "ativa" as const,
      descricao: "O Micromouse está executando o desafio.",
      classes: ["bg-blue-500/10", "text-blue-400"],
    },
    {
      estado: "concluida" as const,
      rotulo: "Desafio cumprido",
      severidade: "sucesso" as const,
      descricao: "O objetivo foi alcançado com sucesso.",
      classes: ["bg-emerald-500/10", "text-emerald-400"],
    },
    {
      estado: "falha" as const,
      rotulo: "Desafio não cumprido",
      severidade: "erro" as const,
      descricao: "A sessão foi encerrada sem alcançar o objetivo.",
      classes: ["bg-rose-500/10", "text-rose-400"],
    },
  ])("renderiza %s com texto e classe corretos", ({
    estado,
    rotulo,
    severidade,
    descricao,
    classes,
  }) => {
    render(
      <EstadoAtualCard
        estado={estado}
        rotulo={rotulo}
        severidade={severidade}
      />,
    );

    const heading = screen.getByRole("heading", { name: rotulo });
    const card = heading.closest("section");

    expect(card).not.toBeNull();
    expect(card).toHaveClass(...classes);
    expect(screen.getByText(descricao)).toBeInTheDocument();
  });
});
