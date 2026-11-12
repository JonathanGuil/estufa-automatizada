const INFO = {
  falha_aquec: {
    nome: "Aquecimento",
    texto: "Falha: a temperatura caiu com o aquecedor ligado",
    cor: "--c-aquec",
    cat: "clima",
    alerta: true,
  },
  salto_umid1: {
    nome: "Sensor zona 1",
    texto: "Salto na leitura de umidade (descartada)",
    cor: "--c-z1",
    cat: "irrigacao",
    alerta: true,
  },
  salto_umid2: {
    nome: "Sensor zona 2",
    texto: "Salto na leitura de umidade (descartada)",
    cor: "--c-z2",
    cat: "irrigacao",
    alerta: true,
  },
  irrig_pulada: {
    nome: "Irrigação",
    texto: "Dose agendada pulada: solo já úmido",
    cor: "--c-valv",
    cat: "irrigacao",
  },
  wifi_off: {
    nome: "ESP32",
    texto: "Wi-Fi caiu",
    cor: "--faint",
    cat: "sistema",
    alerta: true,
  },
  wifi_on: {
    nome: "ESP32",
    texto: "Wi-Fi reconectado",
    cor: "--faint",
    cat: "sistema",
  },
  mqtt_on: {
    nome: "ESP32",
    texto: "Conectado ao servidor",
    cor: "--faint",
    cat: "sistema",
  },
  boot: { nome: "ESP32", texto: "Reiniciou", cor: "--faint", cat: "sistema" },
  config_set: {
    nome: "Painel web",
    texto: "Parâmetros aplicados no ESP32",
    cor: "--accent",
    cat: "sistema",
  },
  comando: {
    nome: "Painel web",
    texto: "Comando executado no ESP32",
    cor: "--accent",
    cat: "sistema",
  },
  config: {
    nome: "Painel local",
    texto: "Parâmetros alterados no painel do ESP32",
    cor: "--accent",
    cat: "sistema",
  },
  log_apagado: {
    nome: "Log local",
    texto: "Dia mais antigo apagado do log (já enviado)",
    cor: "--faint",
    cat: "sistema",
  },
  log_descartado: {
    nome: "Log local",
    texto: "Dia apagado do log antes de chegar à VM",
    cor: "--faint",
    cat: "sistema",
    alerta: true,
  },
  log_cheio: {
    nome: "Log local",
    texto: "Log parou de gravar; as leituras seguem só para a VM",
    cor: "--faint",
    cat: "sistema",
    alerta: true,
  },
  log_normalizado: {
    nome: "Log local",
    texto: "Log voltou a gravar",
    cor: "--faint",
    cat: "sistema",
  },
  log_falha: {
    nome: "Log local",
    texto: "Falha ao apagar um dia antigo do log",
    cor: "--faint",
    cat: "sistema",
    alerta: true,
  },
  log_quase_cheio: {
    nome: "Log local",
    texto: "Memória do ESP32 quase cheia",
    cor: "--faint",
    cat: "sistema",
    alerta: true,
  },
  dose_retomada: {
    nome: "Irrigação",
    texto: "Dose interrompida retomada após reinício",
    cor: "--c-valv",
    cat: "irrigacao",
  },
  dose_abandonada: {
    nome: "Irrigação",
    texto: "Dose interrompida não foi completada",
    cor: "--c-valv",
    cat: "irrigacao",
    alerta: true,
  },
};
// Dia de um arquivo do log a partir do detalhe do evento ("20261103.csv" -> "o dia 03/11")
const diaArquivo = (d) => {
  const m = /^(\d{4})(\d{2})(\d{2})/.exec((d && d.arquivo) || "");
  return m ? `o dia ${m[3]}/${m[2]}` : "um dia";
};
const infoDe = (t) =>
  INFO[t] || { nome: "Sistema", texto: t, cor: "--faint", cat: "sistema" };
const ehAlerta = (t) => !!(INFO[t] && INFO[t].alerta);
// Junta liga e desliga numa linha com duração e agrupa pulsos repetidos (ex.: circulação)
function linhasEventos(evs) {
  const usados = new Set();
  const linhas = [];
  evs.forEach((e, i) => {
    if (usados.has(i)) return;
    const m = ATUADORES[e.tipo];
    if (m && e.estado === 1) {
      let j = -1;
      for (let k = i + 1; k < evs.length; k++) {
        if (evs[k].tipo === e.tipo) {
          if (evs[k].estado === 0) j = k;
          break;
        }
      }
      if (j >= 0) {
        usados.add(j);
        linhas.push({
          tipo: e.tipo,
          t: e.t,
          fim: evs[j].t,
          origem: e.origem,
          fimOrigem: evs[j].origem,
          k: "run",
        });
      } else
        linhas.push({
          tipo: e.tipo,
          t: e.t,
          fim: null,
          origem: e.origem,
          k: ultAtu[e.tipo] === 1 || ultAtu[e.tipo] === true ? "aberto" : "on",
        });
    } else if (m)
      linhas.push({
        tipo: e.tipo,
        t: e.t,
        origem: e.origem,
        k: e.estado === 0 ? "off" : "chg",
      });
    else
      linhas.push({
        tipo: e.tipo,
        t: e.t,
        origem: e.origem,
        k: "info",
        det: e.det,
      });
  });
  linhas.sort((a, b) => a.t - b.t);
  // agrupa pulsos do mesmo atuador e origem com duração parecida (ex.: circulação 5/25 min),
  // mesmo que outros eventos aconteçam no meio; um intervalo de mais de 3 h fecha o grupo
  const out = [];
  const abertos = {};
  linhas.forEach((l) => {
    if (l.k !== "run") {
      out.push(l);
      return;
    }
    const chave = l.tipo + "|" + l.origem;
    const g = abertos[chave];
    if (g) {
      const gd = g.itens[0].fim - g.itens[0].t;
      const ult = g.itens[g.itens.length - 1];
      if (
        Math.abs(gd - (l.fim - l.t)) <= 90000 &&
        l.t - ult.fim <= 3 * 3600000
      ) {
        g.itens.push(l);
        return;
      }
    }
    const novo = { ...l, itens: [l] };
    abertos[chave] = novo;
    out.push(novo);
  });
  return out;
}
const fimDe = (l) => (l.k === "run" ? l.itens[l.itens.length - 1].fim : l.t);
const corAt = (k) => `var(${ATUADORES[k] ? ATUADORES[k].cor : infoDe(k).cor})`;
function origemTag(o) {
  const t = origemTxt(o);
  const c = /manual|painel/.test(t)
    ? "manual"
    : t === "segurança"
      ? "seg"
      : t === "configuração"
        ? "cfg"
        : "";
  return `<span class="tag ${c}">${esc(t)}</span>`;
}
function textoLinha(l) {
  const v = l.tipo.startsWith("valv");
  const ligada = v ? "Aberta" : "Ligada";
  if (l.k === "run") {
    if (l.itens.length > 1) {
      const qtd = `${l.itens.length} ${v ? "aberturas" : "pulsos"}`;
      const d = fmtDur(l.itens[0].fim - l.itens[0].t);
      const ini = fmtHm(l.itens[0].t);
      const fim = fmtHm(l.itens[l.itens.length - 1].fim);
      return `${qtd} de ${d} <span class="muted">· ${ini}–${fim}</span>`;
    }
    const porSeg =
      l.fimOrigem === "seguranca" && l.origem !== "seguranca"
        ? ` <span class="muted">· ${v ? "fechada" : "desligada"} pela segurança</span>`
        : "";
    return `${ligada} <span class="muted">· ${fmtHm(l.t)}–${fmtHm(l.fim)}</span>${porSeg}`;
  }
  if (l.k === "aberto")
    return `<span class="live-run">${ligada} agora</span> <span class="muted">· desde ${fmtHm(l.t)}</span>`;
  if (l.k === "on") return v ? "Abriu" : "Ligou";
  if (l.k === "off")
    return `${ligada} até ${fmtHm(l.t)} <span class="muted">· início antes do período</span>`;
  if (l.k === "chg") return "Mudou de estado";
  const d = l.det || {};
  let extra = "";
  if (l.tipo === "falha_aquec" && d.temp != null)
    extra = ` <span class="muted">· ${Number(d.temp).toFixed(1)} °C</span>`;
  if (d.arquivo) extra = ` <span class="muted">· ${esc(diaArquivo(d))}</span>`;
  if (l.tipo === "log_quase_cheio" && d.uso_pct != null)
    extra = ` <span class="muted">· ${esc(d.uso_pct)}% ocupado</span>`;
  if (l.tipo.startsWith("dose_") && d.zona != null) {
    const falta = d.falta_s != null ? `, faltavam ${esc(d.falta_s)} s` : "";
    extra = ` <span class="muted">· zona ${esc(d.zona)}${falta}</span>`;
  }
  return esc(infoDe(l.tipo).texto) + extra;
}
const nomeLinha = (l) =>
  ATUADORES[l.tipo] ? ATUADORES[l.tipo].nome : infoDe(l.tipo).nome;
const durLinha = (l) =>
  l.k === "run"
    ? fmtDur(l.itens.reduce((a, x) => a + (x.fim - x.t), 0))
    : l.k === "aberto"
      ? fmtDur(Date.now() - l.t)
      : "—";
function htmlResumo(l) {
  return `
    <div class="e">
      <span class="t">${fmtQuando(fimDe(l))}</span>
      <span class="sw" style="background:${corAt(l.tipo)}"></span>
      <span class="x"><b>${esc(nomeLinha(l))}</b> · ${textoLinha(l)}</span>
      ${origemTag(l.origem)}
    </div>`;
}
function pintarEventosResumo(evs) {
  const ult = linhasEventos(evs)
    .sort((a, b) => fimDe(b) - fimDe(a))
    .slice(0, 6);
  $("#evResumo").innerHTML = ult.length
    ? ult.map(htmlResumo).join("")
    : `<div class="empty">Nenhum evento nas últimas 24 h.</div>`;
}
const catDe = (t) => (ATUADORES[t] ? ATUADORES[t].sub : infoDe(t).cat);
const FILTROS = [
  ["", "Todos", () => true],
  ["clima", "Clima", (t) => catDe(t) === "clima"],
  ["irrigacao", "Irrigação", (t) => catDe(t) === "irrigacao"],
  ["luz", "Iluminação", (t) => catDe(t) === "luz"],
  ["sistema", "Sistema", (t) => catDe(t) === "sistema"],
];
let evFiltro = "";
// Linha escondida de um pulso do grupo (aparece ao clicar em "ver todos")
function linhaPulso(x, grupo) {
  return `
    <tr class="sub" data-g="${grupo}" hidden>
      <td></td>
      <td class="a">${fmtHm(x.t)} → ${fmtHm(x.fim)}</td>
      <td></td>
      <td class="d r">${fmtDur(x.fim - x.t)}</td>
      <td></td>
    </tr>`;
}
function pintarEventos(evs) {
  const linhas = linhasEventos(evs);
  $("#evFiltro").innerHTML = FILTROS.map(([k, n, f]) => {
    const c = linhas.filter((l) => f(l.tipo)).length;
    const qtd = `<span class="muted">${c}</span>`;
    return `<button aria-pressed="${k === evFiltro}" data-evf="${k}">${n} ${qtd}</button>`;
  }).join("");
  const f = FILTROS.find((x) => x[0] === evFiltro)[2];
  const lista = linhas
    .filter((l) => f(l.tipo))
    .sort((a, b) => fimDe(b) - fimDe(a));
  const nAl = linhas.filter((l) => ehAlerta(l.tipo)).length;
  $("#evCount").textContent = nAl
    ? `${nAl} alerta${nAl > 1 ? "s" : ""} no período`
    : "";
  let html = "";
  let dia = "";
  let gi = 0;
  const hoje = new Date().toDateString();
  const ontem = new Date(Date.now() - 86400000).toDateString();
  lista.forEach((l) => {
    const fimT = fimDe(l);
    const d = new Date(fimT);
    const ds = d.toDateString();
    if (ds !== dia) {
      dia = ds;
      const rot =
        ds === hoje
          ? "Hoje"
          : ds === ontem
            ? "Ontem"
            : d.toLocaleDateString("pt-BR", { weekday: "long" });
      const data = d.toLocaleDateString("pt-BR", {
        day: "2-digit",
        month: "long",
      });
      html += `<tr class="day"><th colspan="5">${rot}<span>${data}</span></th></tr>`;
    }
    const grupo = l.k === "run" && l.itens.length > 1;
    const id = "g" + gi++;
    const verTodos = grupo
      ? `<button class="expand" data-exp="${id}" aria-expanded="false">ver todos</button>`
      : "";
    html += `
      <tr class="${ehAlerta(l.tipo) ? "alert" : ""}">
        <td class="h">${fmtHm(fimT)}</td>
        <td class="a">
          <span class="nm">
            <span class="sw" style="background:${corAt(l.tipo)}"></span>
            ${esc(nomeLinha(l))}
          </span>
        </td>
        <td class="w">${textoLinha(l)}${verTodos}</td>
        <td class="d r">${durLinha(l)}</td>
        <td>${origemTag(l.origem)}</td>
      </tr>`;
    if (grupo)
      html += l.itens
        .slice()
        .reverse()
        .map((x) => linhaPulso(x, id))
        .join("");
  });
  $("#eventos").innerHTML =
    html ||
    `<tr><td colspan="5" class="empty">Nenhum evento neste filtro e período.</td></tr>`;
}
document.addEventListener("click", (e) => {
  const f = e.target.closest("[data-evf]");
  if (f) {
    evFiltro = f.dataset.evf;
    const c = cacheEv[periodo];
    if (c) pintarEventos(c.rows);
    return;
  }
  const x = e.target.closest("[data-exp]");
  if (x) {
    const ab = x.getAttribute("aria-expanded") !== "true";
    x.setAttribute("aria-expanded", ab);
    x.textContent = ab ? "ocultar" : "ver todos";
    $$(`tr[data-g="${x.dataset.exp}"]`).forEach((r) => {
      r.hidden = !ab;
    });
  }
});
$("#hZona").onchange = () => desenhar("registro");
