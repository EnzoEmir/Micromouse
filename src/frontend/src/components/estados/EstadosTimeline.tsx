import type { EstadoHistoricoItem } from "../../types/estados";
import { estadoParaRotulo } from "./estadoLabels";

type EstadosTimelineProps = {
  historico: EstadoHistoricoItem[];
};

export function EstadosTimeline({ historico }: EstadosTimelineProps) {
  return (
    <section className="rounded-2xl border border-zinc-800 bg-zinc-950 p-6 shadow-sm">
      <div className="mb-4">
        <p className="text-xs font-semibold uppercase tracking-wide text-zinc-500">
          Histórico de transições
        </p>
        <h2 className="text-xl font-semibold text-zinc-100">
          Fluxo do desafio em tempo real
        </h2>
      </div>

      <div className="max-h-[420px] overflow-auto rounded-xl border border-zinc-800">
        <table className="min-w-full text-sm">
          <thead className="sticky top-0 bg-zinc-900/40 text-left text-xs uppercase tracking-wide text-zinc-500">
            <tr>
              <th className="px-4 py-3">Tempo</th>
              <th className="px-4 py-3">De</th>
              <th className="px-4 py-3">Para</th>
              <th className="px-4 py-3">Gatilho</th>
            </tr>
          </thead>

          <tbody className="divide-y divide-zinc-800">
            {historico.map((item, index) => (
              <tr
                key={`${item.tempo}-${index}`}
                className="transition hover:bg-zinc-900/30"
              >
                <td className="px-4 py-3 text-zinc-500">
                  {item.tempo}
                </td>

                <td className="px-4 py-3 text-zinc-500">
                  {item.de ? estadoParaRotulo(item.de) : "—"}
                </td>

                <td className="px-4 py-3 font-medium text-zinc-100">
                  {estadoParaRotulo(item.para)}
                </td>

                <td className="px-4 py-3 text-zinc-400">
                  {item.gatilho}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}