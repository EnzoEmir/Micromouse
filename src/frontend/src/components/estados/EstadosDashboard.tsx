import type { UseTelemetriaReturn } from "../../hooks/useTelemetria";
import { EstadoAtualCard } from "./EstadoAtualCard";
import { EstadosMetrics } from "./EstadosMetrics";
import { EstadosTimeline } from "./EstadosTimeline";
import { useEstados } from "../../hooks/useEstados";

type EstadosDashboardProps = {
  telemetria: UseTelemetriaReturn;
};

export function EstadosDashboard({ telemetria }: EstadosDashboardProps) {
  const estados = useEstados(telemetria);

  return (
    <div className="grid gap-6 lg:grid-cols-[1.35fr_0.9fr]">
      <section className="space-y-6">
        <EstadoAtualCard
          estado={estados.estadoAtual}
          rotulo={estados.rotuloAtual}
          severidade={estados.severidadeAtual}
        />

        <EstadosTimeline historico={estados.historico} />
      </section>

      <aside className="space-y-6">
        <EstadosMetrics fases={estados.fases} />
      </aside>
    </div>
  );
}