export type EstadoDesafio =
  | "aguardando"
  | "em_andamento"
  | "concluida"
  | "falha";

export type SeveridadeEstado = "pendente" | "ativa" | "sucesso" | "erro";

export interface EstadoHistoricoItem {
  tempo: string;
  de: EstadoDesafio | null;
  para: EstadoDesafio;
  gatilho: string;
}

export interface EstadoFaseItem {
  estado: EstadoDesafio;
  rotulo: string;
  duracaoMs: number;
}

export interface EstadoPainel {
  estadoAtual: EstadoDesafio;
  rotuloAtual: string;
  severidadeAtual: SeveridadeEstado;
  historico: EstadoHistoricoItem[];
  fases: EstadoFaseItem[];
}