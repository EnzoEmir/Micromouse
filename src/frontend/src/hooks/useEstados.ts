import { useEffect, useMemo, useRef, useState } from "react";

import type { UseTelemetriaReturn } from "./useTelemetria";
import type {
  EstadoDesafio,
  EstadoFaseItem,
  EstadoHistoricoItem,
  EstadoPainel,
  SeveridadeEstado,
} from "../types/estados";

type TelemetriaIndicadores = UseTelemetriaReturn["indicadores"];
type SessionKey = number | null;

const formatterHora = new Intl.DateTimeFormat("pt-BR", {
  timeStyle: "medium",
});

function formatarHoraAgora(): string {
  return formatterHora.format(new Date());
}

function criarFasesIniciais(): EstadoFaseItem[] {
  return [
    {
      estado: "aguardando",
      rotulo: "Aguardando início",
      duracaoMs: 0,
    },
    {
      estado: "em_andamento",
      rotulo: "Desafio em andamento",
      duracaoMs: 0,
    },
    {
      estado: "concluida",
      rotulo: "Desafio cumprido",
      duracaoMs: 0,
    },
    {
      estado: "falha",
      rotulo: "Desafio não cumprido",
      duracaoMs: 0,
    },
  ];
}

function obterEstadoAtual(indicadores: TelemetriaIndicadores): EstadoDesafio {
  const status = indicadores?.status_corrida?.toLowerCase?.();
  const sucesso = indicadores?.sucesso;

  if (!status || status === "aguardando") {
    return "aguardando";
  }

  if (status === "em_andamento") {
    return "em_andamento";
  }

  if (status === "concluida") {
    return sucesso === true ? "concluida" : "falha";
  }

  if (status === "falha") {
    return "falha";
  }

  return "aguardando";
}

function rotuloDoEstado(estado: EstadoDesafio): string {
  switch (estado) {
    case "em_andamento":
      return "Desafio em andamento";
    case "concluida":
      return "Desafio cumprido";
    case "falha":
      return "Desafio não cumprido";
    default:
      return "Aguardando início";
  }
}

function severidadeDoEstado(estado: EstadoDesafio): SeveridadeEstado {
  switch (estado) {
    case "concluida":
      return "sucesso";
    case "falha":
      return "erro";
    case "em_andamento":
      return "ativa";
    default:
      return "pendente";
  }
}

function obterGatilhoInicial(
  estado: EstadoDesafio,
  sessionKey: SessionKey,
): string {
  if (sessionKey === null) {
    return "Aguardando início da sessão";
  }

  switch (estado) {
    case "em_andamento":
      return "Sessão ativa detectada";
    case "concluida":
      return "Sessão concluída com sucesso detectada";
    case "falha":
      return "Sessão encerrada sem sucesso detectada";
    default:
      return "Estado inicial detectado";
  }
}

function obterGatilhoTransicao(
  estadoAnterior: EstadoDesafio,
  estadoAtual: EstadoDesafio,
): string {
  if (estadoAtual === "em_andamento") {
    return estadoAnterior === "aguardando"
      ? "Sessão iniciada"
      : "Corrida em andamento";
  }

  if (estadoAtual === "concluida") {
    return "Micromouse alcançou a área central";
  }

  if (estadoAtual === "falha") {
    return "Sessão encerrada sem atingir o objetivo";
  }

  return "Atualização recebida do backend";
}

export function useEstados(
  telemetria: UseTelemetriaReturn,
): EstadoPainel {
  const estadoAtual = useMemo(
    () => obterEstadoAtual(telemetria.indicadores),
    [telemetria.indicadores],
  );

  const sessionKey: SessionKey = telemetria.indicadores.id_corrida_banco ?? null;

  const [historico, setHistorico] = useState<EstadoHistoricoItem[]>([]);
  const [fases, setFases] = useState<EstadoFaseItem[]>(() =>
    criarFasesIniciais(),
  );

  const ultimoEstadoRef = useRef<EstadoDesafio | null>(null);
  const ultimaSessionKeyRef = useRef<SessionKey | undefined>(undefined);
  const ultimoTimestampRef = useRef<number>(Date.now());

  useEffect(() => {
    const agora = Date.now();
    const sessionAlterada = ultimaSessionKeyRef.current !== sessionKey;

    if (sessionAlterada) {
      ultimaSessionKeyRef.current = sessionKey;
      ultimoEstadoRef.current = estadoAtual;
      ultimoTimestampRef.current = agora;
      setFases(criarFasesIniciais());
      setHistorico([
        {
          tempo: formatarHoraAgora(),
          de: null,
          para: estadoAtual,
          gatilho: obterGatilhoInicial(estadoAtual, sessionKey),
        },
      ]);
      return;
    }

    if (ultimoEstadoRef.current === null) {
      ultimoEstadoRef.current = estadoAtual;
      ultimoTimestampRef.current = agora;
      setHistorico([
        {
          tempo: formatarHoraAgora(),
          de: null,
          para: estadoAtual,
          gatilho: obterGatilhoInicial(estadoAtual, sessionKey),
        },
      ]);
      return;
    }

    if (ultimoEstadoRef.current === estadoAtual) {
      return;
    }

    const estadoAnterior = ultimoEstadoRef.current;
    const duracao = agora - ultimoTimestampRef.current;

    setFases((prev) =>
      prev.map((fase) =>
        fase.estado === estadoAnterior
          ? { ...fase, duracaoMs: fase.duracaoMs + duracao }
          : fase,
      ),
    );

    setHistorico((prev) => [
      {
        tempo: formatarHoraAgora(),
        de: estadoAnterior,
        para: estadoAtual,
        gatilho: obterGatilhoTransicao(estadoAnterior, estadoAtual),
      },
      ...prev,
    ]);

    ultimoEstadoRef.current = estadoAtual;
    ultimoTimestampRef.current = agora;
  }, [estadoAtual, sessionKey]);

  return {
    estadoAtual,
    rotuloAtual: rotuloDoEstado(estadoAtual),
    severidadeAtual: severidadeDoEstado(estadoAtual),
    historico,
    fases,
  };
}