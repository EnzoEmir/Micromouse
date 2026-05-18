import { DashboardIndicadores } from '../components/DashboardIndicadores';
import { MonitoringLayout } from '../components/MonitoringLayout';

type TelemetriaPageProps = {
  activeView: 'telemetria' | 'labirinto';
  onNavigateTelemetria: () => void;
  onNavigateLabirinto: () => void;
};

export function TelemetriaPage({
  activeView,
  onNavigateTelemetria,
  onNavigateLabirinto,
}: TelemetriaPageProps) {
  return (
    <MonitoringLayout
      activeView={activeView}
      onNavigateTelemetria={onNavigateTelemetria}
      onNavigateLabirinto={onNavigateLabirinto}
      eyebrow="Telemetria"
      title="Métricas em tempo real do robô MM-07"
      description="Acompanhe os indicadores exigidos para avaliação da corrida: bateria, velocidade média e tempo de execução."
    >
      <DashboardIndicadores />
    </MonitoringLayout>
  );
}