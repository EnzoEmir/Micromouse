/**
 * Hook React para conexão WebSocket de telemetria em tempo real.
 *
 * Uso:
 * ```tsx
 * const { indicadores, enviarPacote, conectado, erro } = useTelemetria();
 * ```
 */

import { useCallback, useEffect, useRef, useState } from "react";
import toast from "react-hot-toast";

import type {
  ConfigSessao,
  IndicadoresDesempenho,
  PacoteTelemetria,
} from "../types/telemetria";
import { WS_TELEMETRIA_URL } from "../services/telemetria";

type StatusConexaoMicromouse = "online" | "offline" | "waiting";

type MovimentacaoTelemetria = {
  id_corrida: number;
  timestamp_ms: number;
  x: number;
  y: number;
  w: number;
  paredes: ParedesCelula;
};

type ParedesCelula = {
  norte: boolean;
  sul: boolean;
  leste: boolean;
  oeste: boolean;
};

/** Estado inicial dos indicadores (espelha criar_estado_inicial do backend). */
const ESTADO_INICIAL: IndicadoresDesempenho = {
  id_corrida_banco: null,
  sessao_hardware_id: null,
  bateria_inicial: null,
  bateria_atual: null,
  bateria_final: null,
  velocidade_media: null,
  tempo_decorrido_ms: 0,
  tempo_final_ms: null,
  status_corrida: "aguardando",
  sucesso: null,
  ultimo_timestamp_ms: null,
  alerta_bateria_critica: false,
  alerta_dado_invalido: false,
};

const CONFIG_SESSAO_INICIAL: ConfigSessao = {
  dimensao: null,
  tentativa: null,
};

/** Intervalo entre tentativas de reconexão (ms). */
const RECONNECT_INTERVAL_MS = 3000;

export interface UseTelemetriaReturn {
  /** Estado atual dos indicadores de desempenho. */
  indicadores: IndicadoresDesempenho;
  /** Dados de configuração da sessão (recebidos uma única vez). */
  configSessao: ConfigSessao;
  /** Estado atual da conexão com o Micromouse. */
  statusConexao: StatusConexaoMicromouse;
  /** Última mensagem enviada pelo backend sobre a conexão. */
  mensagemStatusConexao: string | null;
  /** Envia um pacote de telemetria via WebSocket. */
  enviarPacote: (pacote: PacoteTelemetria) => void;
  /** Indica se o WebSocket está conectado. */
  conectado: boolean;
  /** Última mensagem de erro, se houver. */
  erro: string | null;
  /** Ultima movimentacao recebida (mudanca de celula). */
  ultimaMovimentacao: MovimentacaoTelemetria | null;
}

export function useTelemetria(): UseTelemetriaReturn {
  const [indicadores, setIndicadores] = useState<IndicadoresDesempenho>(() => {
    const indicadoresSalvos = localStorage.getItem("indicadores");
    return indicadoresSalvos ? JSON.parse(indicadoresSalvos) : ESTADO_INICIAL;
  });

  const [configSessao, setConfigSessao] = useState<ConfigSessao>(() => {
    const configSessaoSalva = localStorage.getItem("configSessao");
    return configSessaoSalva
      ? JSON.parse(configSessaoSalva)
      : CONFIG_SESSAO_INICIAL;
  });

  const [conectado, setConectado] = useState(false);
  const [erro, setErro] = useState<string | null>(null);
  const [statusConexao, setStatusConexao] =
    useState<StatusConexaoMicromouse>("waiting");
  const [mensagemStatusConexao, setMensagemStatusConexao] = useState<
    string | null
  >(null);
  const [ultimaMovimentacao, setUltimaMovimentacao] =
    useState<MovimentacaoTelemetria | null>(null);

  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Decodifica o inteiro de paredes (w) em um binário para identificar as paredes presentes na célula.
  const decodificarParedes = useCallback((w: number): ParedesCelula => {
    // o "&" é o operador bit-a-bit AND. Ele “pega” apenas o bit indicado.
    return {
      norte: (w & 1) === 1, //Se (w & 1) === 1, o bit 0 esta ligado → parede ao norte.
      sul: (w & 2) === 2, //Se (w & 2) === 2, o bit 1 esta ligado → parede ao sul.
      leste: (w & 4) === 4, //Se (w & 4) === 4, o bit 2 esta ligado → parede ao leste.
      oeste: (w & 8) === 8, //Se (w & 8) === 8, o bit 3 esta ligado → parede ao oeste.
    };
  }, []);

  const isMovimentacaoPayload = useCallback(
    (
      data: unknown,
    ): data is {
      id_corrida: number;
      timestamp_ms: number;
      x: number;
      y: number;
      w: number;
    } => {
      if (!data || typeof data !== "object") {
        return false;
      }

      const payload = data as Record<string, unknown>;

      return (
        typeof payload.id_corrida === "number" &&
        typeof payload.timestamp_ms === "number" &&
        typeof payload.x === "number" &&
        typeof payload.y === "number" &&
        typeof payload.w === "number"
      );
    },
    [],
  );

  //Persistir estado no localStorage para manter dados entre recarregamentos
  useEffect(() => {
    localStorage.setItem("indicadores", JSON.stringify(indicadores));
    localStorage.setItem("configSessao", JSON.stringify(configSessao));
  }, [indicadores, configSessao]);

  const realizarConexao = useCallback(function conectar() {
    // Evitar conexões duplicadas
    if (
      wsRef.current &&
      (wsRef.current.readyState === WebSocket.OPEN ||
        wsRef.current.readyState === WebSocket.CONNECTING)
    ) {
      return;
    }

    const ws = new WebSocket(WS_TELEMETRIA_URL);

    ws.onopen = () => {
      setConectado(true);
      setErro(null);
    };

    ws.onmessage = (event) => {
      try {
        const parsed = JSON.parse(event.data);
        const payload = parsed?.data ?? parsed;
        console.log("[useTelemetria] Mensagem recebida INICIAL:", parsed);
        // Tratar erros enviados pelo backend
        if (parsed.type === "ERROR") {
          toast.error(parsed.message || "Erro na telemetria", {
            id: "telemetria-error", // Evita toasts duplicados
          });
          return;
        }

        if (parsed.type === "CONNECTION_STATUS") {
          setStatusConexao(
            parsed.data?.status === "offline" ? "offline" : "online",
          );
          setMensagemStatusConexao(parsed.data?.message ?? null);
          return;
        }

        if (parsed.type === "SESSAO_ENCERRADA") {
          console.log(
            "[useTelemetria] Sessão anterior encerrada:",
            parsed.data,
          );
          setIndicadores(ESTADO_INICIAL);
          setConfigSessao(CONFIG_SESSAO_INICIAL);
          setStatusConexao("waiting");
          setMensagemStatusConexao(null);
          return;
        }

        if (
          parsed?.type === "MOVIMENTACAO" ||
          parsed?.type === "MOVIMENTACAO_PAREDES" ||
          isMovimentacaoPayload(payload)
        ) {
          // console.log("[useTelemetria] Movimentação recebida:", payload);
          if (isMovimentacaoPayload(payload)) {
            setUltimaMovimentacao({
              ...payload,
              paredes: decodificarParedes(payload.w),
            });
          }
          return;
        }

        const pacote = parsed?.data;

        if (parsed.type === "SESSAO_INICIADA") {
          const { dimensao, tentativa, ...indicadoresData } = pacote;
          setIndicadores(indicadoresData as IndicadoresDesempenho);
          setConfigSessao({ dimensao, tentativa });
          setStatusConexao("online");
          setMensagemStatusConexao(null);
        } else if (parsed.type === "ATUALIZACAO_TELEMETRIA") {
          setIndicadores(pacote as IndicadoresDesempenho);
          setStatusConexao("online");
          setMensagemStatusConexao(null);
          // console.log("Payload: ", payload);
          // console.log("Pacote ", pacote);
          if (isMovimentacaoPayload(payload)) {
            setUltimaMovimentacao({
              ...payload,
              paredes: decodificarParedes(payload.w),
            });
          }
        }
      } catch (e) {
        console.error("[useTelemetria] Erro ao processar mensagem:", e);
      }
    };

    ws.onerror = () => {
      setErro("Erro na conexão WebSocket.");
    };

    ws.onclose = () => {
      setConectado(false);
      wsRef.current = null;

      // Reconexão automática
      reconnectTimerRef.current = setTimeout(conectar, RECONNECT_INTERVAL_MS);
    };

    wsRef.current = ws;
  }, []);

  useEffect(() => {
    realizarConexao();

    return () => {
      // Cleanup ao desmontar
      if (reconnectTimerRef.current) {
        clearTimeout(reconnectTimerRef.current);
      }
      if (wsRef.current) {
        wsRef.current.close();
      }
    };
  }, [realizarConexao]);

  const enviarPacote = useCallback((pacote: PacoteTelemetria) => {
    if (wsRef.current && wsRef.current.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(pacote));
    } else {
      console.warn(
        "[useTelemetria] WebSocket não conectado, pacote descartado.",
      );
    }
  }, []);

  return {
    indicadores,
    configSessao,
    statusConexao,
    mensagemStatusConexao,
    enviarPacote,
    conectado,
    erro,
    ultimaMovimentacao,
  };
}
