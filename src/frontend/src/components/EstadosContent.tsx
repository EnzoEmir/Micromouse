import React, { useState, useEffect, useRef } from 'react';

type TipoAlertaTelemetria = 'bateria_critica' | 'possivel_parada_inesperada';

interface AlertaTelemetria {
  tipo: TipoAlertaTelemetria;
  mensagem: string;
  timestamp_ms: number;
}

// Estados visuais mapeados para a esteira de progresso do robô
type EstadoCorridaVisual = 'configuracao' | 'em_movimento' | 'rota_otimizada' | 'concluida' | 'falha';

interface IndicadoresDesempenho {
  id_corrida_banco: number | null;
  sessao_hardware_id: number | null;
  bateria_inicial: number | null;
  bateria_atual: number | null;
  bateria_final: number | null;
  velocidade_media: number | null;
  tempo_decorrido_ms: number;
  tempo_final_ms: number | null;
  status_corrida: 'em_andamento' | 'concluida' | 'falha' | null;
  sucesso: boolean | null;
  ultimo_timestamp_ms: number | null;
  alerta_bateria_critica: boolean;
  alerta_possivel_parada_inesperada: boolean;
  alerta_dado_invalido: boolean;
  log_alertas: AlertaTelemetria[];
  
  // Propriedades opcionais injetadas em pacotes brutos ou eventos iniciais
  dimensao?: number | string;
  tentativa?: number;
  rota?: [number, number][]; // Coordenadas do trajeto ideal calculado pelo Floodfill
}

interface ItemHistorico {
  tempo: string;
  de: string;
  para: string;
  gatilho: string;
  tipoLog: 'Estado' | 'Erro';
}

export function EstadosContent(): React.JSX.Element {
  const [websocketConectado, setWebsocketConectado] = useState<boolean>(false);
  const [dadosCorrida, setDadosCorrida] = useState<IndicadoresDesempenho | null>(null);
  
  // Controla qual card visual da esteira está ativo
  const [estadoVisual, setEstadoVisual] = useState<EstadoCorridaVisual>('configuracao');
  const [historicoTransicoes, setHistoricoTransicoes] = useState<ItemHistorico[]>([]);
  const [filtroHistorico, setFiltroHistorico] = useState<'Tudo' | 'Estado' | 'Erro'>('Tudo');

  // Refs para cálculo incremental estável dos tempos das fases
  const tempoConfigRef = useRef<number>(0);
  const tempoMovimentoRef = useRef<number>(0);

  const [tempoConfig, setTempoConfig] = useState<number>(0);
  const [tempoMovimento, setTempoMovimento] = useState<number>(0);
  const [tempoRota, setTempoRota] = useState<number>(0);

  const adicionarAoHistorico = (de: string, para: string, gatilho: string, tipoLog: 'Estado' | 'Erro' = 'Estado'): void => {
    const novoEvento: ItemHistorico = {
      tempo: new Date().toLocaleTimeString(),
      de,
      para,
      gatilho,
      tipoLog
    };
    setHistoricoTransicoes(prev => [novoEvento, ...prev]);
  };

  useEffect(() => {
    const ws = new WebSocket('ws://localhost:8000/api/telemetria/ws');

    ws.onopen = () => {
      setWebsocketConectado(true);
      console.log('Monitor de Estados conectado ao fluxo unificado do backend.');
    };

    ws.onmessage = (event: MessageEvent) => {
      const pacoteRaw = JSON.parse(event.data);
      if (!pacoteRaw) return;

      // Suporta o envelope padrão do backend {"type": "...", "data": {...}} ou pacote direto
      const dados: IndicadoresDesempenho = pacoteRaw.data ? pacoteRaw.data : pacoteRaw;
      const tipoEventoBackend = pacoteRaw.type; 

      setDadosCorrida(dados);
      
      const tempoAtual = dados.tempo_decorrido_ms ?? 0;
      let proximoEstadoVisual: EstadoCorridaVisual | null = null;
      let gatilhoMsg = '';
      let tipoLogTransicao: 'Estado' | 'Erro' = 'Estado';

      // 1. Árvore de Decisão de Estados com bypass para Rota Otimizada do Passo 3
      if (tipoEventoBackend === 'SESSAO_INICIADA') {
        proximoEstadoVisual = 'configuracao';
        tempoConfigRef.current = tempoAtual;
        setTempoConfig(tempoAtual);
        gatilhoMsg = `Sessão Inicializada: Labirinto definido como ${dados.dimensao ?? '—'} (Tentativa ${dados.tentativa ?? 1})`;
      } 
      // BYPASS CRÍTICO: Se a chave 'rota' vier preenchida com coordenadas, salta para 'rota_otimizada'
      else if (dados.rota && dados.rota.length > 0) {
        proximoEstadoVisual = 'rota_otimizada';
        const diferencaRota = tempoAtual - (tempoConfigRef.current + tempoMovimentoRef.current);
        setTempoRota(diferencaRota > 0 ? diferencaRota : 0);
        gatilhoMsg = `Pacote 3 Recebido: Algoritmo Floodfill enviou a matriz de trajeto ideal com ${dados.rota.length} nós.`;
      }
      else if (dados.status_corrida === 'em_andamento') {
        proximoEstadoVisual = 'em_movimento';
        const diferenca = tempoAtual - tempoConfigRef.current;
        tempoMovimentoRef.current = diferenca > 0 ? diferenca : 0;
        setTempoMovimento(tempoMovimentoRef.current);
        gatilhoMsg = `Telemetria Atualizada: Robô explorando células do labirinto (Tempo: ${tempoAtual}ms).`;
      } 
      else if (dados.status_corrida === 'concluida') {
        proximoEstadoVisual = 'concluida';
        gatilhoMsg = 'Corrida Finalizada: O Centro do labirinto foi alcançado com sucesso de forma autónoma!';
      } 
      else if (dados.status_corrida === 'falha') {
        proximoEstadoVisual = 'falha';
        tipoLogTransicao = 'Erro';
        gatilhoMsg = 'Corrida Interrompida: Falha detetada no percurso (Colisão ou Abandono).';
      }

      // 2. Registar transição apenas se houver uma alteração real de estado
      if (proximoEstadoVisual && proximoEstadoVisual !== estadoVisual) {
        const obterLabel = (id: EstadoCorridaVisual) => {
          if (id === 'configuracao') return 'Configuração Inicial';
          if (id === 'em_movimento') return 'Mapeamento Inicial';
          if (id === 'rota_otimizada') return 'Rota Otimizada';
          return id === 'concluida' ? 'Desafio Cumprido' : 'Corrida Interrompida';
        };

        adicionarAoHistorico(obterLabel(estadoVisual), obterLabel(proximoEstadoVisual), gatilhoMsg, tipoLogTransicao);
        setEstadoVisual(proximoEstadoVisual);
      }
    };

    ws.onclose = () => setWebsocketConectado(false);
    return () => ws.close();
  }, [estadoVisual]);

  const formatarSegundos = (ms: number) => `${(ms / 1000).toFixed(1)}s`;

  const logsFiltrados = historicoTransicoes.filter(log => {
    if (filtroHistorico === 'Tudo') return true;
    return log.tipoLog === filtroHistorico;
  });

  const obterIndicePasso = () => {
    if (estadoVisual === 'configuracao') return 0;
    if (estadoVisual === 'em_movimento') return 1;
    if (estadoVisual === 'rota_otimizada') return 2;
    return 3; // Fim de corrida (Concluída ou Falha)
  };

  const passoAtual = obterIndicePasso();

  return (
    <section className="mx-auto w-full max-w-7xl p-4 text-zinc-600 bg-zinc-50/50 min-h-screen">
      {/* Cabeçalho de Contexto */}
      <div className="mb-6 flex justify-between items-center border-b border-zinc-200 pb-4">
        <div>
          <span className="text-xs font-mono text-zinc-400">ID Banco: #{dadosCorrida?.id_corrida_banco ?? '—'} | Hardware: #{dadosCorrida?.sessao_hardware_id ?? '—'}</span>
          <h1 className="text-xl font-semibold text-zinc-800">Painel de Monitoramento — Robô MM-07</h1>
        </div>
        <span className={`inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-xs font-medium shadow-2xs border ${
          websocketConectado ? 'bg-blue-50 text-blue-700 border-blue-200' : 'bg-red-50 text-red-700 border-red-200'
        }`}>
          <span className={`h-2 w-2 rounded-full ${websocketConectado ? 'bg-blue-500 animate-pulse' : 'bg-red-500'}`} />
          {websocketConectado ? 'Online' : 'Offline'}
        </span>
      </div>

      {/* Esteira de Estados */}
      <div className="bg-white p-6 rounded-xl border border-zinc-200 mb-6 shadow-2xs">
        <div className="flex items-center justify-between mb-6">
          <div>
            <h2 className="text-sm font-semibold text-zinc-800">Máquina de Estados</h2>
            <p className="text-xs text-zinc-400">Fluxo em tempo real baseado no payload processado</p>
          </div>
          <div className="flex gap-4 text-xs font-medium">
            <span className="flex items-center gap-1.5 text-zinc-500"><span className="h-2 w-2 rounded-full bg-blue-600" /> Atual</span>
            <span className="flex items-center gap-1.5 text-zinc-500"><span className="h-2 w-2 rounded-full bg-zinc-800" /> Concluído</span>
            <span className="flex items-center gap-1.5 text-zinc-500"><span className="h-2 w-2 rounded-full bg-zinc-200" /> Pendente</span>
          </div>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
          {/* Card 1 */}
          <div className={`p-4 rounded-xl border transition-all ${passoAtual === 0 ? 'border-blue-600 bg-blue-50/20 ring-1 ring-blue-500/10' : 'border-zinc-200 bg-zinc-50/50'}`}>
            <div className="flex items-center gap-2">
              <span className={`h-4 w-4 rounded-full flex items-center justify-center text-[10px] text-white font-bold ${passoAtual > 0 ? 'bg-zinc-800' : 'bg-blue-600'}`}>
                {passoAtual > 0 ? '✓' : '1'}
              </span>
              <span className="text-[10px] font-bold text-zinc-400 uppercase tracking-wider">CONFIGURAÇÃO</span>
            </div>
            <span className={`text-xs font-semibold block mt-1.5 ${passoAtual === 0 ? 'text-blue-700' : 'text-zinc-700'}`}>Configuração Validada</span>
          </div>

          {/* Card 2 */}
          <div className={`p-4 rounded-xl border transition-all ${passoAtual === 1 ? 'border-blue-600 bg-blue-50/20 ring-1 ring-blue-500/10' : passoAtual > 1 ? 'border-zinc-200 bg-zinc-50/50' : 'border-zinc-100 opacity-50 bg-white'}`}>
            <div className="flex items-center gap-2">
              <span className={`h-4 w-4 rounded-full flex items-center justify-center text-[10px] text-white font-bold ${passoAtual > 1 ? 'bg-zinc-800' : 'bg-blue-600'}`}>
                {passoAtual > 1 ? '✓' : '2'}
              </span>
              <span className="text-[10px] font-bold text-zinc-400 uppercase tracking-wider">MAPEAMENTO</span>
            </div>
            <span className={`text-xs font-semibold block mt-1.5 ${passoAtual === 1 ? 'text-blue-700' : 'text-zinc-700'}`}>Exploração Ativa</span>
          </div>

          {/* Card 3 */}
          <div className={`p-4 rounded-xl border transition-all ${passoAtual === 2 ? 'border-blue-600 bg-blue-50/20 ring-1 ring-blue-500/10' : passoAtual > 2 ? 'border-zinc-200 bg-zinc-50/50' : 'border-zinc-100 opacity-50 bg-white'}`}>
            <div className="flex items-center gap-2">
              <span className={`h-4 w-4 rounded-full flex items-center justify-center text-[10px] text-white font-bold ${passoAtual > 2 ? 'bg-zinc-800' : 'bg-blue-600'}`}>
                {passoAtual > 2 ? '✓' : '3'}
              </span>
              <span className="text-[10px] font-bold text-zinc-400 uppercase tracking-wider">EXECUÇÃO</span>
            </div>
            <span className={`text-xs font-semibold block mt-1.5 ${passoAtual === 2 ? 'text-blue-700' : 'text-zinc-700'}`}>Rota Otimizada</span>
          </div>

          {/* Card 4 */}
          <div className={`p-4 rounded-xl border transition-all ${estadoVisual === 'concluida' ? 'border-green-600 bg-green-50/30' : estadoVisual === 'falha' ? 'border-red-600 bg-red-50/30' : 'border-zinc-100 opacity-50 bg-white'}`}>
            <div className="flex items-center gap-2">
              <span className={`h-4 w-4 rounded-full flex items-center justify-center text-[10px] text-white font-bold ${estadoVisual === 'concluida' ? 'bg-green-600' : estadoVisual === 'falha' ? 'bg-red-600' : 'bg-zinc-300'}`}>4</span>
              <span className="text-[10px] font-bold text-zinc-400 uppercase tracking-wider">ENCERRAMENTO</span>
            </div>
            <span className={`text-xs font-semibold block mt-1.5 ${estadoVisual === 'concluida' ? 'text-green-700' : estadoVisual === 'falha' ? 'text-red-700' : 'text-zinc-400'}`}>
              {estadoVisual === 'concluida' ? 'Desafio Cumprido!' : estadoVisual === 'falha' ? 'Corrida Interrompida' : 'Aguardando Finalização'}
            </span>
          </div>
        </div>
      </div>

      {/* Grid Inferior de Conteúdo */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        
        {/* Tabela Histórica */}
        <div className="lg:col-span-2 bg-white p-6 rounded-xl border border-zinc-200 shadow-2xs">
          <div className="flex items-center justify-between mb-4">
            <div>
              <h2 className="text-sm font-semibold text-zinc-800">Histórico de Transições</h2>
              <p className="text-xs text-zinc-400">Logs gerados de acordo com os pacotes recebidos</p>
            </div>
            <div className="flex rounded-md bg-zinc-100 p-0.5 text-xs font-medium text-zinc-600">
              {(['Tudo', 'Estado', 'Erro'] as const).map(tipo => (
                <button
                  key={tipo}
                  onClick={() => setFiltroHistorico(tipo)}
                  className={`rounded px-3 py-1 transition-all ${filtroHistorico === tipo ? 'bg-white text-zinc-950 shadow-2xs font-semibold' : 'hover:text-zinc-950'}`}
                >
                  {tipo}
                </button>
              ))}
            </div>
          </div>

          <div className="overflow-x-auto">
            <table className="w-full text-left border-collapse">
              <thead>
                <tr className="border-b border-zinc-100 text-xs text-zinc-400 font-medium">
                  <th className="py-2.5 font-mono">Tempo</th>
                  <th className="py-2.5">De</th>
                  <th className="py-2.5">Para</th>
                  <th className="py-2.5">Gatilho</th>
                </tr>
              </thead>
              <tbody className="text-xs">
                {logsFiltrados.length === 0 ? (
                  <tr>
                    <td colSpan={4} className="py-12 text-center text-zinc-400">Aguardando atualizações da telemetria...</td>
                  </tr>
                ) : (
                  logsFiltrados.map((transicao, i) => (
                    <tr key={i} className="border-b border-zinc-50 hover:bg-zinc-50/50">
                      <td className="py-3 text-zinc-400 font-mono">{transicao.tempo}</td>
                      <td className="py-3 text-zinc-500">{transicao.de}</td>
                      <td className={`py-3 font-medium ${transicao.tipoLog === 'Erro' ? 'text-red-600' : 'text-blue-600'}`}>➔ {transicao.para}</td>
                      <td className="py-3 text-zinc-500 font-sans">{transicao.gatilho}</td>
                    </tr>
                  ))
                )}
              </tbody>
            </table>
          </div>
        </div>

        {/* Métricas Globais */}
        <div className="bg-white p-6 rounded-xl border border-zinc-200 shadow-2xs flex flex-col justify-between">
          <div>
            <h2 className="text-sm font-semibold text-zinc-800 mb-1">Métricas por Fase</h2>
            <p className="text-xs text-zinc-400 mb-4">Tempo acumulado gasto em cada estágio</p>
            
            <div className="space-y-4">
              <div>
                <div className="flex justify-between text-xs mb-1">
                  <span className="font-medium text-zinc-700">Configuração Inicial</span>
                  <span className="font-mono text-zinc-500">{formatarSegundos(tempoConfig)}</span>
                </div>
                <div className="w-full h-1.5 bg-zinc-100 rounded-full overflow-hidden">
                  <div className="h-full bg-blue-600 transition-all duration-300" style={{ width: tempoConfig > 0 ? '100%' : '0%' }} />
                </div>
              </div>

              <div>
                <div className="flex justify-between text-xs mb-1">
                  <span className="font-medium text-zinc-700">Mapeamento e Varredura</span>
                  <span className="font-mono text-zinc-500">{formatarSegundos(tempoMovimento)}</span>
                </div>
                <div className="w-full h-1.5 bg-zinc-100 rounded-full overflow-hidden">
                  <div className="h-full bg-blue-600 transition-all duration-300" style={{ width: estadoVisual !== 'configuracao' ? '100%' : '0%' }} />
                </div>
              </div>

              <div>
                <div className="flex justify-between text-xs mb-1">
                  <span className="font-medium text-zinc-700">Cálculo e Rota Otimizada</span>
                  <span className="font-mono text-zinc-500">{formatarSegundos(tempoRota)}</span>
                </div>
                <div className="w-full h-1.5 bg-zinc-100 rounded-full overflow-hidden">
                  <div className="h-full bg-blue-600 transition-all duration-300" style={{ width: (estadoVisual === 'rota_otimizada' || passoAtual === 3) ? '100%' : '0%' }} />
                </div>
              </div>
            </div>
          </div>

          <div className="border-t border-zinc-100 pt-4 mt-6 space-y-2 text-xs">
            <div className="flex justify-between">
              <span className="text-zinc-400">Tempo Total da Corrida:</span>
              <span className="font-mono font-bold text-zinc-800">{formatarSegundos(dadosCorrida?.tempo_decorrido_ms ?? 0)}</span>
            </div>
            <div className="flex justify-between">
              <span className="text-zinc-400">Velocidade Média:</span>
              <span className="font-semibold text-zinc-700">{dadosCorrida?.velocidade_media ? `${dadosCorrida.velocidade_media.toFixed(2)} m/s` : '—'}</span>
            </div>
            <div className="flex justify-between">
              <span className="text-zinc-400">Nível da Bateria:</span>
              <span className="font-semibold text-zinc-700">{dadosCorrida?.bateria_atual ?? dadosCorrida?.bateria_inicial ?? '—'}%</span>
            </div>
          </div>
        </div>

      </div>
    </section>
  );
}