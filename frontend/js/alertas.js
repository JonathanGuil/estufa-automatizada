// Faixa com os alertas das últimas 24 h: log local do ESP32 e dose não completada.
// "Ok" esconde a faixa até aparecer um alerta mais novo (guardado só neste navegador).
const CHAVE_ALERTA_VISTO = "estufa.alertaVisto";
function lerVisto() {
  try {
    return Number(localStorage.getItem(CHAVE_ALERTA_VISTO)) || 0;
  } catch (e) {
    return 0;
  }
}
function gravarVisto(t) {
  try {
    localStorage.setItem(CHAVE_ALERTA_VISTO, String(t));
  } catch (e) {}
}
let alertaVistoMem = 0;

function alertasAtivos(evs) {
  const ultimo = (tipos) => {
    for (let i = evs.length - 1; i >= 0; i--)
      if (tipos.includes(evs[i].tipo)) return evs[i];
    return null;
  };
  const out = [];
  const cheio = ultimo(["log_cheio", "log_normalizado"]);
  if (cheio && cheio.tipo === "log_cheio")
    out.push({
      t: cheio.t,
      txt:
        `O log local do ESP32 parou de gravar (${fmtQuando(cheio.ts)}). ` +
        "As leituras seguem só para a VM; sem conexão, elas se perdem.",
    });
  const falha = ultimo(["log_falha", "log_apagado"]);
  if (falha && falha.tipo === "log_falha")
    out.push({
      t: falha.t,
      txt:
        `O ESP32 não conseguiu apagar ${diaArquivo(falha.det)} ` +
        `do log local (${fmtQuando(falha.ts)}).`,
    });
  const quase = ultimo(["log_quase_cheio"]);
  if (quase)
    out.push({
      t: quase.t,
      txt: `A memória do ESP32 passou de ${quase.det.uso_pct ?? 85}% (${fmtQuando(quase.ts)}).`,
    });
  evs
    .filter((e) => e.tipo === "log_descartado")
    .forEach((e) =>
      out.push({
        t: e.t,
        txt:
          `O ESP32 apagou ${diaArquivo(e.det)} do log antes de ele chegar ` +
          `à VM (${fmtQuando(e.ts)}).`,
      }),
    );
  evs
    .filter((e) => e.tipo === "dose_abandonada")
    .forEach((e) =>
      out.push({
        t: e.t,
        txt:
          `A dose da zona ${e.det.zona ?? "?"} foi interrompida por reinícios ` +
          `e não foi completada (${fmtQuando(e.ts)}).`,
      }),
    );
  return out.sort((a, b) => b.t - a.t);
}
function pintarAlertas() {
  const lista = alertasAtivos(eventos24 || []);
  const visto = Math.max(lerVisto(), alertaVistoMem);
  const novo = lista.length && lista[0].t > visto;
  $("#alertBanner").hidden = !novo;
  if (novo)
    $("#alertLista").innerHTML = lista
      .map((a) => `<li>${esc(a.txt)}</li>`)
      .join("");
}
$("#alertOk").onclick = () => {
  const lista = alertasAtivos(eventos24 || []);
  if (lista.length) {
    alertaVistoMem = lista[0].t;
    gravarVisto(lista[0].t);
  }
  $("#alertBanner").hidden = true;
};
