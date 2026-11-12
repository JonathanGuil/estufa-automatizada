// Gráficos em SVG, sem biblioteca externa.
const W = 820;
const ML = 48;
const MR = 12;
const fmtEixo = (t, horas) =>
  new Date(t).toLocaleString(
    "pt-BR",
    horas > 48
      ? { day: "2-digit", month: "2-digit" }
      : { hour: "2-digit", minute: "2-digit" },
  );
function reduzir(pts, max = 1600) {
  if (pts.length <= max) return pts;
  const n = Math.ceil(pts.length / (max / 2));
  const out = [];
  for (let i = 0; i < pts.length; i += n) {
    const b = pts.slice(i, i + n);
    let lo = b[0];
    let hi = b[0];
    for (const p of b) {
      if (p.v < lo.v) lo = p;
      if (p.v > hi.v) hi = p;
    }
    if (lo === hi) out.push(lo);
    else if (lo.t < hi.t) out.push(lo, hi);
    else out.push(hi, lo);
  }
  return out;
}
function niceStep(x) {
  const e = Math.pow(10, Math.floor(Math.log10(x)));
  const f = x / e;
  return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10) * e;
}
function grafLinha({
  series,
  t0,
  t1,
  h = 190,
  casas = 0,
  unid = "",
  marcas = [],
  horas = 24,
}) {
  series = series.map((se) => ({ ...se, pts: reduzir(se.pts) }));
  const pts = series.flatMap((s) => s.pts);
  if (!pts.length) return `<div class="empty">Sem leituras no período.</div>`;
  const mt = 10;
  const mb = 24;
  const iw = W - ML - MR;
  const ih = h - mt - mb;
  const base = mt + ih;
  let vmin = Math.min(...pts.map((p) => p.v));
  let vmax = Math.max(...pts.map((p) => p.v));
  const passo = niceStep((vmax - vmin) / 3 || 1);
  vmin = Math.floor(vmin / passo) * passo;
  vmax = Math.ceil(vmax / passo) * passo;
  if (vmax === vmin) vmax = vmin + passo;
  const nT = Math.round((vmax - vmin) / passo);
  const ct = passo < 1 ? (passo < 0.1 ? 2 : 1) : 0;
  const X = (t) => ML + ((t - t0) / (t1 - t0)) * iw;
  const Y = (v) => mt + (1 - (v - vmin) / (vmax - vmin)) * ih;
  let s = `<svg class="ch" viewBox="0 0 ${W} ${h}" role="img">`;
  for (let i = 0; i <= nT; i++) {
    const v = vmin + passo * i;
    const y = Y(v);
    const yLinha = y.toFixed(1);
    const yTexto = (y + 3.5).toFixed(1);
    s += `<line x1="${ML}" x2="${W - MR}" y1="${yLinha}" y2="${yLinha}" stroke="var(--line)"/>`;
    s += `<text x="${ML - 8}" y="${yTexto}" text-anchor="end">${v.toFixed(ct)}${unid}</text>`;
  }
  for (let i = 0; i <= 4; i++) {
    const t = t0 + ((t1 - t0) * i) / 4;
    const x = X(t).toFixed(1);
    const ancora = i === 0 ? "start" : i === 4 ? "end" : "middle";
    s += `<text x="${x}" y="${h - 6}" text-anchor="${ancora}">${fmtEixo(t, horas)}</text>`;
  }
  marcas.forEach((m) => {
    if (m.t < t0) return;
    const x = X(m.t).toFixed(1);
    s += `<line x1="${x}" x2="${x}" y1="${mt}" y2="${base}"
      stroke="var(${m.cor})" stroke-dasharray="3 3"/>`;
  });
  series.forEach((se) => {
    if (!se.pts.length) return;
    const d = se.pts
      .map(
        (p, i) => `${i ? "L" : "M"}${X(p.t).toFixed(1)},${Y(p.v).toFixed(1)}`,
      )
      .join("");
    const primeiro = se.pts[0];
    const ultimo = se.pts[se.pts.length - 1];
    if (se.area) {
      const fecha = `L${X(ultimo.t).toFixed(1)},${base}L${X(primeiro.t).toFixed(1)},${base}Z`;
      s += `<path d="${d}${fecha}" fill="var(${se.cor})" opacity=".07"/>`;
    }
    const tracejado = se.dash ? 'stroke-dasharray="6 4"' : "";
    s += `<path d="${d}" fill="none" stroke="var(${se.cor})" stroke-width="1.8"
      stroke-linejoin="round" ${tracejado}/>`;
    s += `<circle cx="${X(ultimo.t).toFixed(1)}" cy="${Y(ultimo.v).toFixed(1)}" r="3.5"
      fill="var(${se.cor})"/>`;
  });
  s += `<line class="cross" x1="0" x2="0" y1="${mt}" y2="${base}"
    stroke="var(--muted)" visibility="hidden"/>`;
  return { svg: s + "</svg>", meta: { t0, t1, series, casas, unid, X, iw } };
}
const nomeCurto = (nome) =>
  nome
    .replace("Válvula zona ", "Válv. ")
    .replace("Ventilação", "Vent.")
    .replace("Exaustão", "Exaust.")
    .replace("Aquecimento", "Aquec.")
    .replace("Iluminação", "Luz");
function grafFaixas(keys, evs, t0, t1) {
  const rh = 16;
  const gap = 7;
  const iw = W - ML - MR;
  const X = (t) => ML + ((t - t0) / (t1 - t0)) * iw;
  const h = keys.length * (rh + gap);
  let s = `<svg class="ch" viewBox="0 0 ${W} ${h}" role="img"
    aria-label="Linha do tempo dos atuadores">`;
  keys.forEach((k, i) => {
    const y = i * (rh + gap);
    const meta = ATUADORES[k];
    const runs = intervalos(k, evs, t0, t1);
    const tot = runs.reduce((a, [x, y]) => a + (y - x), 0);
    const resumo = `${meta.nome}: ${runs.length} acionamento(s), ${fmtDur(tot)} ligado`;
    s += `<text x="${ML - 8}" y="${y + 12}" text-anchor="end">${nomeCurto(meta.nome)}</text>`;
    s += `<rect x="${ML}" y="${y}" width="${iw}" height="${rh}" rx="3"
      fill="var(--off-bg)"><title>${resumo}</title></rect>`;
    runs.forEach(([a, b]) => {
      const largura = Math.max(2, X(b) - X(a)).toFixed(1);
      const dica = `${meta.nome}: ${fmtHora(a)} → ${fmtHm(b)} (${fmtDur(b - a)})`;
      s += `<rect x="${X(a).toFixed(1)}" y="${y}" width="${largura}" height="${rh}" rx="2"
        fill="var(${meta.cor})"><title>${dica}</title></rect>`;
    });
    if (!runs.length)
      s += `<text x="${ML + 8}" y="${y + 12}">sem acionamento no período</text>`;
  });
  return s + "</svg>";
}
// tooltip com o valor mais próximo
function ligarTooltip(wrap, meta) {
  const svg = wrap.querySelector("svg");
  if (!svg || !meta) return;
  const tip = document.createElement("div");
  tip.className = "tip";
  tip.hidden = true;
  wrap.appendChild(tip);
  const cross = svg.querySelector(".cross");
  svg.addEventListener("mousemove", (ev) => {
    const r = svg.getBoundingClientRect();
    const x = ((ev.clientX - r.left) / r.width) * W;
    if (x < ML || x > W - MR) {
      tip.hidden = true;
      cross.setAttribute("visibility", "hidden");
      return;
    }
    const t = meta.t0 + ((x - ML) / meta.iw) * (meta.t1 - meta.t0);
    const partes = meta.series
      .map((se) => {
        let best = null;
        let bd = Infinity;
        for (const p of se.pts) {
          const d = Math.abs(p.t - t);
          if (d < bd) {
            bd = d;
            best = p;
          }
        }
        return best
          ? `${se.nome ? se.nome + " " : ""}${best.v.toFixed(meta.casas)}${meta.unid}`
          : null;
      })
      .filter(Boolean);
    cross.setAttribute("x1", x);
    cross.setAttribute("x2", x);
    cross.setAttribute("visibility", "visible");
    tip.hidden = false;
    tip.textContent = `${fmtHora(t)} · ${partes.join(" · ")}`;
    tip.style.left = (x / W) * 100 + "%";
    tip.style.top = "0px";
  });
  svg.addEventListener("mouseleave", () => {
    tip.hidden = true;
    cross.setAttribute("visibility", "hidden");
  });
}
function montar(el, blocos) {
  el.innerHTML = blocos
    .map((b, i) =>
      typeof b === "string"
        ? b
        : `<div class="chartwrap" data-i="${i}">${b.svg}</div>`,
    )
    .join("");
  blocos.forEach((b, i) => {
    if (typeof b !== "string")
      ligarTooltip(el.querySelector(`.chartwrap[data-i="${i}"]`), b.meta);
  });
}
const serie = (rows, f) =>
  rows
    .filter((r) => r[f] != null)
    .map((r) => ({ t: new Date(r.ts).getTime(), v: Number(r[f]) }))
    .filter((p) => !isNaN(p.v));
const titulo = (txt) =>
  `<div class="sub" style="margin:14px 0 4px;font-weight:600">${txt}</div>`;

let periodo = 24;
async function desenhar(view) {
  const horas = view === "geral" ? 24 : periodo;
  const alvo = $("#g-" + view);
  if (!alvo) return;
  if (!alvo.innerHTML) alvo.innerHTML = `<div class="skel"></div>`;
  const [rows, evs] = await Promise.all([
    carregarTel(horas),
    carregarEv(horas),
  ]);
  const t1 = Date.now();
  const t0 = t1 - horas * 3600000;
  const temp = { pts: serie(rows, "temp"), cor: "--c-temp", area: true };
  const z = $("#hZona") ? $("#hZona").value : "todas";
  const umid = [];
  if (view !== "registro" || z !== "2")
    umid.push({ pts: serie(rows, "u1"), cor: "--c-z1", nome: "Z1" });
  if (view !== "registro" || z !== "1")
    umid.push({
      pts: serie(rows, "u2"),
      cor: "--c-z2",
      dash: true,
      nome: "Z2",
    });
  const doses = evs
    .filter((e) => (e.tipo === "valv1" || e.tipo === "valv2") && e.estado === 1)
    .map((e) => ({ t: e.t, cor: "--c-valv" }));
  const todos = ["aquec", "vent", "exaus", "led", "valv1", "valv2"];
  if (view === "geral" || view === "registro") {
    montar(alvo, [
      titulo("Temperatura (°C)"),
      grafLinha({ series: [temp], t0, t1, casas: 1, unid: "°", horas }),
      grafFaixas(todos, evs, t0, t1),
      titulo("Umidade do solo (ADC · maior = mais seco)"),
      grafLinha({ series: umid, t0, t1, h: 160, marcas: doses, horas }),
    ]);
  } else if (view === "clima") {
    montar(alvo, [
      grafLinha({ series: [temp], t0, t1, h: 220, casas: 1, unid: "°", horas }),
      grafFaixas(["aquec", "vent", "exaus"], evs, t0, t1),
    ]);
  } else if (view === "irrigacao") {
    const legenda = `
      <div class="legend" style="margin-top:10px">
        <span style="color:var(--c-z1)"><i></i>Zona 1</span>
        <span style="color:var(--c-z2)"><i class="dash"></i>Zona 2</span>
        <span style="color:var(--c-valv)"><i class="box"></i>Válvula aberta</span>
        <span>Maior ADC = solo mais seco</span>
      </div>`;
    montar(alvo, [
      grafLinha({ series: umid, t0, t1, h: 220, marcas: doses, horas }),
      grafFaixas(["valv1", "valv2"], evs, t0, t1),
      legenda,
    ]);
  } else if (view === "luz") {
    montar(alvo, [grafFaixas(["led"], evs, t0, t1)]);
  }
  if (view === "geral") {
    eventos24 = evs;
    pintarEventosResumo(evs);
    pintarAtuadores(ultAtu);
    pintarEscalas();
  }
  if (view === "registro") pintarEventos(evs);
}
function legendaGantt() {
  const nomes = {
    aquec: "Aquecimento",
    vent: "Ventilação",
    exaus: "Exaustão",
    led: "Iluminação",
    valv1: "Válvulas",
  };
  const atuadores = Object.entries(nomes)
    .map(
      ([k, n]) =>
        `<span style="color:var(${ATUADORES[k].cor})"><i class="box"></i>${n}</span>`,
    )
    .join("");
  return (
    atuadores +
    `<span style="color:var(--c-z1)"><i></i>Zona 1</span>` +
    `<span style="color:var(--c-z2)"><i class="dash"></i>Zona 2</span>`
  );
}
