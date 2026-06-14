import type { EstadoDesafio } from "../../types/estados";

export function estadoParaRotulo(estado: EstadoDesafio): string {
  switch (estado) {
    case "aguardando":
      return "Aguardando início";
    case "em_andamento":
      return "Desafio em andamento";
    case "concluida":
      return "Desafio cumprido";
    case "falha":
      return "Desafio não cumprido";
    default:
      return estado;
  }
}