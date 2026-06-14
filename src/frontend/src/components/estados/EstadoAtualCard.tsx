import type { EstadoDesafio, SeveridadeEstado } from "../../types/estados";

type EstadoAtualCardProps = {
  estado: EstadoDesafio;
  rotulo: string;
  severidade: SeveridadeEstado;
};

const estilos: Record<SeveridadeEstado, string> = {
  pendente: "border-zinc-800 bg-zinc-950 text-zinc-400",
  ativa: "border-blue-500/20 bg-blue-500/10 text-blue-400",
  sucesso: "border-emerald-500/20 bg-emerald-500/10 text-emerald-400",
  erro: "border-rose-500/20 bg-rose-500/10 text-rose-400",
};

export function EstadoAtualCard({
  estado,
  rotulo,
  severidade,
}: EstadoAtualCardProps) {
  return (
    <section className={`rounded-2xl border p-6 shadow-sm ${estilos[severidade]}`}>
      <p className="text-xs font-semibold uppercase tracking-wide opacity-70">
        Estado atual
      </p>

      <h3 className="mt-2 text-3xl font-bold">
        {rotulo}
      </h3>

      <p className="mt-3 text-sm opacity-80">
        {estado === "aguardando" &&
          "Aguardando o início da sessão para exibir o estado do desafio."}

        {estado === "em_andamento" &&
          "O Micromouse está executando o desafio."}

        {estado === "concluida" &&
          "O objetivo foi alcançado com sucesso."}

        {estado === "falha" &&
          "A sessão foi encerrada sem alcançar o objetivo."}
      </p>
    </section>
  );
}