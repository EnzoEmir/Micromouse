import { MonitoringLayout } from "../components/MonitoringLayout";
import { EstadosDashboard } from "../components/estados/EstadosDashboard";
import { useTelemetria } from "../hooks/useTelemetria";

type EstadosPageProps = {
  activeView: "telemetria" | "corridas" | "estados";
  onNavigateTelemetria: () => void;
  onNavigateCorridas: () => void;
  onNavigateEstados: () => void;
};

export function EstadosPage({
  activeView,
  onNavigateTelemetria,
  onNavigateCorridas,
  onNavigateEstados,
}: EstadosPageProps) {
  const telemetria = useTelemetria();

  return (
    <MonitoringLayout
      activeView={activeView}
      onNavigateTelemetria={onNavigateTelemetria}
      onNavigateCorridas={onNavigateCorridas}
      onNavigateEstados={onNavigateEstados}
      eyebrow="Estados"
      title="Máquina de estados da corrida"
      description="Acompanhe em tempo real se o desafio está em andamento, foi cumprido ou terminou sem sucesso."
      statusConexao={telemetria.statusConexao}
      mensagemStatusConexao={telemetria.mensagemStatusConexao}
    >
      <EstadosDashboard telemetria={telemetria} />
    </MonitoringLayout>
  );
}