const ATUADORES = {
  valv1: {
    nome: "Válvula zona 1",
    on: "Aberta",
    off: "Fechada",
    cor: "--c-valv",
    sub: "irrigacao",
  },
  valv2: {
    nome: "Válvula zona 2",
    on: "Aberta",
    off: "Fechada",
    cor: "--c-valv",
    sub: "irrigacao",
  },
  vent: {
    nome: "Ventilação",
    on: "Ligada",
    off: "Desligada",
    cor: "--c-vent",
    sub: "clima",
  },
  exaus: {
    nome: "Exaustão",
    on: "Ligada",
    off: "Desligada",
    cor: "--c-exaus",
    sub: "clima",
  },
  aquec: {
    nome: "Aquecimento",
    on: "Ligado",
    off: "Desligado",
    cor: "--c-aquec",
    sub: "clima",
  },
  led: {
    nome: "Iluminação",
    on: "Ligada",
    off: "Desligada",
    cor: "--c-led",
    sub: "luz",
  },
};
const ORIGEM = {
  logica: "automático",
  manual: "manual",
  seguranca: "segurança",
  segurança: "segurança",
  sistema: "sistema",
  nuvem: "painel web",
  janela: "medição",
  periodico: "periódico",
  retomada: "retomada",
};
const origemTxt = (o) => ORIGEM[o] || o || "—";

let ultAtu = {}; // estado corrente (/status)
const pend = {}; // overrides otimistas: id -> {modo?, ligado?, t}
let confirmando = null; // id de válvula aguardando confirmação inline
function marcarPend(id, campos) {
  pend[id] = Object.assign({}, pend[id], campos, { t: Date.now() });
}
function aplicaPend(a) {
  a = Object.assign({}, a || {});
  a.manual = Object.assign({}, (a && a.manual) || {});
  const agora = Date.now();
  for (const id in pend) {
    const p = pend[id];
    if (agora - p.t > 12000) {
      delete pend[id];
      continue;
    }
    const modoServ =
      a.manual[id] === 1 || a.manual[id] === true ? "manual" : "auto";
    const ligServ = a[id] === 1 || a[id] === true;
    const okModo = p.modo === undefined || p.modo === modoServ;
    const okLig = p.ligado === undefined || p.ligado === ligServ;
    if (okModo && okLig) {
      delete pend[id];
      continue;
    }
    if (p.modo !== undefined) a.manual[id] = p.modo === "manual" ? 1 : 0;
    if (p.ligado !== undefined) a[id] = p.ligado ? 1 : 0;
  }
  return a;
}

// "Por quê": último evento do atuador + parâmetro que governa
let eventos24 = []; // eventos (asc) do período carregado na visão geral
function ultimoEvento(k) {
  for (let i = eventos24.length - 1; i >= 0; i--)
    if (eventos24[i].tipo === k && eventos24[i].estado !== null)
      return eventos24[i];
  return null;
}
const verbo = (k, on) =>
  k.startsWith("valv") ? (on ? "Abriu" : "Fechou") : on ? "Ligou" : "Desligou";
function porque(k, on) {
  const c = cfgAtual || {};
  const A = c.aquecedor || {};
  const V = c.ventilador || {};
  const E = c.exaustor || {};
  const F = c.fotoperiodo || {};
  const CI = c.circulacao || {};
  const IR = c.irrigacao || {};
  const ev = ultimoEvento(k);
  const quando = ev
    ? `${verbo(k, ev.estado === 1)} ${fmtQuando(ev.ts)} · ${origemTxt(ev.origem)}`
    : "Sem eventos nas últimas 24 h";
  const diaNoite = (o, txt) =>
    o.liga_dia_c != null
      ? `${txt} <b>${o.liga_dia_c} °C</b> (dia) / <b>${o.liga_noite_c ?? "—"} °C</b> (noite)`
      : "";
  let regra = "";
  if (k === "aquec") regra = diaNoite(A, "Liga abaixo de");
  if (k === "vent") {
    const t = diaNoite(V, "Liga acima de");
    let ci = "";
    if (CI.habilitado !== false && CI.liga_min != null)
      ci =
        `circulação <b>${CI.liga_min}/${CI.desliga_min} min</b>` +
        ` das ${CI.hora_ini_h} às ${CI.hora_fim_h} h`;
    regra = [t, ci].filter(Boolean).join(" · ");
  }
  if (k === "exaus")
    regra =
      diaNoite(E, "Liga acima de") +
      (CI.aciona_exaustor ? " · pulsa com a circulação" : "");
  if (k === "led" && F.liga_h != null) {
    const liga = String(F.liga_h).padStart(2, "0");
    const desliga = String(F.desliga_h).padStart(2, "0");
    regra = `Fotoperíodo <b>${liga}:00–${desliga}:00</b>`;
  }
  if (k === "valv1" || k === "valv2") {
    const i = k === "valv1" ? 0 : 1;
    const d = arrAt(IR.dose_ml, i);
    const it = arrAt(IR.intervalo_h, i);
    if (d != null) regra = `Dose <b>${d} mL</b> a cada <b>${it} h</b>`;
  }
  return [quando, regra].filter(Boolean).join(" · ");
}
function htmlAtuador(k, a) {
  const meta = ATUADORES[k];
  const on = a[k] === 1 || a[k] === true;
  const man = (a.manual || {})[k] === 1 || (a.manual || {})[k] === true;
  const est = on ? meta.on : meta.off;
  const chip = man
    ? `<span class="chip warn">Manual · ${est}</span>`
    : `<span class="chip ${on ? "on" : "off"}">${est}</span>`;
  let ctl =
    `<span class="seg" role="group" aria-label="Modo de ${meta.nome}">` +
    `<button aria-pressed="${!man}" data-cmd="modo" data-k="${k}" data-v="auto">` +
    "Automático</button>" +
    `<button aria-pressed="${man}" data-cmd="modo" data-k="${k}" data-v="manual">` +
    "Manual</button></span>";
  if (man) {
    if (confirmando === k) {
      ctl += `<span class="confirm">Abrir a válvula? Fecha sozinha em 5 min.
        <button class="btn-s primary" data-cmd="liga" data-k="${k}" data-v="ok">Abrir</button>
        <button class="btn-s" data-cmd="cancela" data-k="${k}">Cancelar</button></span>`;
    } else if (on) {
      const texto = k.startsWith("valv") ? "Fechar" : "Desligar";
      ctl += ` <button class="btn-s" data-cmd="liga" data-k="${k}" data-v="0">${texto}</button>`;
    } else {
      const texto = k.startsWith("valv") ? "Abrir" : "Ligar";
      ctl += ` <button class="btn-s primary" data-cmd="liga" data-k="${k}" data-v="1">${texto}</button>`;
    }
  }
  return `
    <div class="act ${man ? "manual" : ""}">
      <span class="nm">
        <span class="sw" style="background:var(${meta.cor})"></span>
        ${meta.nome}
      </span>
      ${chip}
      <span class="why">${porque(k, on)}</span>
      <span class="ctl">${ctl}</span>
    </div>`;
}
function pintarAtuadores(raw) {
  const a = aplicaPend(raw || {});
  $$("[data-acts]").forEach((el) => {
    el.innerHTML = el.dataset.acts
      .split(",")
      .map((k) => htmlAtuador(k, a))
      .join("");
  });
  // selo de "manual" no menu
  const nMan = { clima: 0, irrigacao: 0, luz: 0 };
  for (const k in ATUADORES)
    if ((a.manual || {})[k] === 1 || (a.manual || {})[k] === true)
      nMan[ATUADORES[k].sub]++;
  $$("[data-badge]").forEach((b) => {
    const n = nMan[b.dataset.badge];
    b.hidden = !n;
    b.textContent = n ? "manual" : "";
    b.title = n ? `${n} atuador(es) em manual` : "";
  });
}
async function comando(id, campos) {
  marcarPend(id, campos);
  pintarAtuadores(ultAtu);
  const body = {};
  body[id] = campos;
  try {
    const r = await api("/comando", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    if (r.status === 401) {
      mostrarLogin();
      return;
    }
  } catch (e) {
    /* rede: o próximo poll reflete o estado real */
  }
  setTimeout(atualizar, 900);
  setTimeout(atualizar, 2500);
}
document.addEventListener("click", (e) => {
  const b = e.target.closest("[data-cmd]");
  if (!b) return;
  const k = b.dataset.k;
  const cmd = b.dataset.cmd;
  const v = b.dataset.v;
  if (cmd === "modo") {
    confirmando = null;
    comando(k, { modo: v });
  } else if (cmd === "cancela") {
    confirmando = null;
    pintarAtuadores(ultAtu);
  } else if (cmd === "liga") {
    if (v === "1" && k.startsWith("valv")) {
      confirmando = k;
      pintarAtuadores(ultAtu);
      return;
    }
    confirmando = null;
    comando(k, { modo: "manual", ligado: v !== "0" });
  }
});
