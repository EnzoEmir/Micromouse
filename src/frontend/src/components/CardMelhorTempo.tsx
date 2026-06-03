/**
 * Componente de destaque visual para o melhor tempo registrado em um labirinto.
 *
 * Reutiliza o padrão de `CardIndicador` de `DashboardIndicadores.tsx`.
 * Chama `useMelhorTempo` internamente — basta passar o `tipo` como prop.
 *
 * Uso:
 * ```tsx
 * <CardMelhorTempo tipo="classico" />
 * ```
 */

import React from "react";

import type { TipoLabirinto } from "../types/corrida";
import { useMelhorTempo } from "../hooks/useMelhorTempo";

// ---------------------------------------------------------------------------
// Helpers (espelham os de DashboardIndicadores)
// ---------------------------------------------------------------------------

const formatarTempo = (ms?: number | null): string => {
  if (ms === null || ms === undefined || Number.isNaN(ms) || ms < 0) {
    return "00:00.000";
  }

  const minutos = Math.floor(ms / 60000);
  const segundos = Math.floor((ms % 60000) / 1000);
  const milissegundos = Math.floor(ms % 1000);

  return `${String(minutos).padStart(2, "0")}:${String(segundos).padStart(
    2,
    "0",
  )}.${String(milissegundos).padStart(3, "0")}`;
};

/**
 * Formata um id_corrida numérico como "#2026-05-01-011".
 *
 * A lógica usa o id como sufixo zero-padded de 3 dígitos combinado com a
 * data atual; em produção, o backend pode enviar o id já formatado — ajuste
 * conforme necessário.
 */
const formatarIdCorrida = (id: number): string => {
  const hoje = new Date();
  const ano = hoje.getFullYear();
  const mes = String(hoje.getMonth() + 1).padStart(2, "0");
  const dia = String(hoje.getDate()).padStart(2, "0");
  return `#${ano}-${mes}-${dia}-${String(id).padStart(3, "0")}`;
};

const formatarData = (dataIso: string | null): string => {
  if (!dataIso) return "--";

  try {
    return new Intl.DateTimeFormat("pt-BR", {
      day: "2-digit",
      month: "2-digit",
      year: "numeric",
      hour: "2-digit",
      minute: "2-digit",
    }).format(new Date(dataIso));
  } catch {
    return dataIso;
  }
};

// ---------------------------------------------------------------------------
// CardIndicador — cópia local para não criar dependência circular
// (igual ao de DashboardIndicadores.tsx)
// ---------------------------------------------------------------------------

type CardIndicadorProps = {
  titulo: string;
  valor: string;
  descricao?: string;
  estado?: "normal" | "critico" | "sucesso" | "vazio";
};

const CardIndicador: React.FC<CardIndicadorProps> = ({
  titulo,
  valor,
  descricao,
  estado = "normal",
}) => {
  const estilos = {
    normal: "border-neutral-200 bg-white text-neutral-900",
    critico: "border-red-300 bg-red-50 text-red-950",
    sucesso: "border-green-300 bg-green-50 text-green-950",
    vazio: "border-neutral-200 bg-neutral-50 text-neutral-400",
  }[estado];

  return (
    <section className={`rounded-xl border p-5 transition-colors ${estilos}`}>
      <p className="text-xs font-semibold uppercase tracking-wide text-neutral-500">
        {titulo}
      </p>
      <p className="mt-3 text-3xl font-semibold tracking-tight">{valor}</p>
      {descricao && (
        <p className="mt-2 text-xs text-current opacity-75">{descricao}</p>
      )}
    </section>
  );
};

// ---------------------------------------------------------------------------
// CardMelhorTempo
// ---------------------------------------------------------------------------

export type CardMelhorTempoProps = {
  /** Tipo do labirinto para buscar o melhor tempo. */
  tipo: TipoLabirinto;
};

export const CardMelhorTempo: React.FC<CardMelhorTempoProps> = ({ tipo }) => {
  const { melhorTempo, loading, erro } = useMelhorTempo(tipo);

  // ── Estado: carregando
  if (loading) {
    return (
      <div className="w-full rounded-2xl border border-neutral-200 bg-white p-6 shadow-sm">
        <header className="mb-6 border-b border-neutral-100 pb-4">
          <p className="text-xs font-semibold uppercase tracking-wide text-neutral-500">
            Recorde
          </p>
          <h2 className="text-2xl font-semibold text-neutral-950">
            Melhor Resultado
          </h2>
          <p className="mt-1 text-sm text-neutral-500">
            Menor tempo com desafio cumprido para este labirinto.
          </p>
        </header>

        <div className="grid grid-cols-1 gap-4 md:grid-cols-3">
          {(["Sessão", "Tempo Total", "Conquistado em"] as const).map(
            (titulo) => (
              <CardIndicador
                key={titulo}
                titulo={titulo}
                valor="--"
                estado="vazio"
              />
            ),
          )}
        </div>
      </div>
    );
  }

  // ── Estado: erro
  if (erro) {
    return (
      <div className="w-full rounded-2xl border border-red-200 bg-red-50 p-6 shadow-sm">
        <p className="text-sm font-medium text-red-700">
          ⚠️ Não foi possível carregar o melhor tempo: {erro}
        </p>
      </div>
    );
  }

  // ── Estado: vazio (nenhuma corrida concluída com sucesso)
  if (!melhorTempo) {
    return (
      <div className="w-full rounded-2xl border border-neutral-200 bg-white p-6 shadow-sm">
        <header className="mb-6 border-b border-neutral-100 pb-4">
          <p className="text-xs font-semibold uppercase tracking-wide text-neutral-500">
            Recorde
          </p>
          <h2 className="text-2xl font-semibold text-neutral-950">
            Melhor Resultado
          </h2>
          <p className="mt-1 text-sm text-neutral-500">
            Menor tempo com desafio cumprido para este labirinto.
          </p>
        </header>

        <div className="grid grid-cols-1 gap-4 md:grid-cols-3">
          <CardIndicador
            titulo="Sessão"
            valor="Nenhum desafio concluído ainda"
            estado="vazio"
          />
          <CardIndicador titulo="Tempo Total" valor="--" estado="vazio" />
          <CardIndicador
            titulo="Conquistado em"
            valor="--"
            estado="vazio"
          />
        </div>
      </div>
    );
  }

  // ── Estado: com resultado
  return (
    <div className="w-full rounded-2xl border border-neutral-200 bg-white p-6 shadow-sm">
      <header className="mb-6 flex flex-col gap-3 border-b border-neutral-100 pb-4 md:flex-row md:items-start md:justify-between">
        <div>
          <p className="text-xs font-semibold uppercase tracking-wide text-neutral-500">
            Recorde
          </p>
          <h2 className="text-2xl font-semibold text-neutral-950">
            Melhor Resultado
          </h2>
          <p className="mt-1 text-sm text-neutral-500">
            Menor tempo com desafio cumprido para este labirinto.
          </p>
        </div>

        <span className="self-start rounded-full bg-green-100 px-3 py-1 text-xs font-medium text-green-700">
          🏆 Recorde registrado
        </span>
      </header>

      <main className="grid grid-cols-1 gap-4 md:grid-cols-3">
        <CardIndicador
          titulo="Sessão"
          valor={formatarIdCorrida(melhorTempo.id_corrida)}
          descricao="ID da corrida recordista"
          estado="sucesso"
        />

        <CardIndicador
          titulo="Tempo Total"
          valor={formatarTempo(melhorTempo.tempo_total)}
          descricao="Menor tempo com desafio cumprido"
          estado="sucesso"
        />

        <CardIndicador
          titulo="Conquistado em"
          valor={formatarData(melhorTempo.data_hora_fim)}
          descricao="Data e hora da conquista"
          estado="sucesso"
        />
      </main>
    </div>
  );
};