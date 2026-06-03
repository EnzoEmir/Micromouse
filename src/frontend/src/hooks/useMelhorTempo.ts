/**
 * Hook React para buscar e manter o melhor tempo de um labirinto.
 *
 * Uso:
 * ```tsx
 * const { melhorTempo, loading, erro, refetch } = useMelhorTempo("classico");
 * ```
 */

import { useCallback, useEffect, useState } from "react";

import type { MelhorTempoResponse } from "../types/corrida";
import { fetchMelhorTempo } from "../services/corrida";

export interface UseMelhorTempoReturn {
  /** Dados do melhor tempo, ou `null` se nenhum desafio foi concluído. */
  melhorTempo: MelhorTempoResponse | null;
  /** `true` enquanto a requisição está em andamento. */
  loading: boolean;
  /** Mensagem de erro, ou `null` se não houver erro. */
  erro: string | null;
  /** Reexecuta a busca manualmente (ex: após nova corrida encerrada). */
  refetch: () => void;
}

/**
 * Busca o melhor tempo para o `tipo` de labirinto informado.
 * Reexecuta automaticamente quando `tipo` muda.
 */
export function useMelhorTempo(tipo: string): UseMelhorTempoReturn {
  const [melhorTempo, setMelhorTempo] = useState<MelhorTempoResponse | null>(
    null,
  );
  const [loading, setLoading] = useState(true);
  const [erro, setErro] = useState<string | null>(null);
  // Contador interno usado para disparar re-fetches via refetch()
  const [contador, setContador] = useState(0);

  const refetch = useCallback(() => {
    setContador((c) => c + 1);
  }, []);

  useEffect(() => {
    let cancelado = false;

    async function buscar() {
      setLoading(true);
      setErro(null);

      try {
        const resultado = await fetchMelhorTempo(tipo);

        if (!cancelado) {
          setMelhorTempo(resultado);
        }
      } catch (e) {
        if (!cancelado) {
          setErro(
            e instanceof Error ? e.message : "Erro ao buscar melhor tempo.",
          );
        }
      } finally {
        if (!cancelado) {
          setLoading(false);
        }
      }
    }

    buscar();

    return () => {
      cancelado = true;
    };
    // `contador` como dependência garante que refetch() re-execute o efeito
  }, [tipo, contador]);

  return { melhorTempo, loading, erro, refetch };
}