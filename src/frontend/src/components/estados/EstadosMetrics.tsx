import type { EstadoFaseItem } from "../../types/estados";

type EstadosMetricsProps = {
  fases: EstadoFaseItem[];
};

const maximo = (fases: EstadoFaseItem[]) =>
  Math.max(...fases.map((fase) => fase.duracaoMs), 1);

function formatarDuracao(ms: number): string {
  if (ms <= 0) return "0.0s";
  return `${(ms / 1000).toFixed(1)}s`;
}

export function EstadosMetrics({ fases }: EstadosMetricsProps) {
  const maior = maximo(fases);

  return (
    <section className="rounded-2xl border border-zinc-800 bg-zinc-950 p-6 shadow-sm">
      <p className="text-xs font-semibold uppercase tracking-wide text-zinc-500">
        Métricas por fase
      </p>
      <h2 className="mt-1 text-xl font-semibold text-zinc-100">
        Tempo gasto em cada estado
      </h2>

      <div className="mt-5 space-y-4">
        {fases.map((fase) => {
          const largura = Math.max((fase.duracaoMs / maior) * 100, 2);

          return (
            <div key={fase.estado}>
              <div className="mb-1 flex items-center justify-between text-sm">
                <span className="font-medium text-zinc-300">
                  {fase.rotulo}
                </span>
                <span className="text-zinc-500">
                  {formatarDuracao(fase.duracaoMs)}
                </span>
              </div>

              <div className="h-2 rounded-full bg-zinc-900">
                <div
                  className={`h-2 rounded-full ${
                    fase.estado === "concluida"
                      ? "bg-emerald-500"
                      : fase.estado === "falha"
                        ? "bg-rose-500"
                        : fase.estado === "em_andamento"
                          ? "bg-blue-500"
                          : "bg-zinc-500"
                  }`}
                  style={{ width: `${largura}%` }}
                />
              </div>
            </div>
          );
        })}
      </div>
    </section>
  );
}