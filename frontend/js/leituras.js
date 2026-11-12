let ultimaTel = null;
function pintarStatus(s) {
  const on = s && s.online;
  const parado =
    s && s.ultima_telemetria
      ? Date.now() - new Date(s.ultima_telemetria).getTime()
      : 0;
  const semDados = on && parado > (s.sem_dados_min || 15) * 60000;
  $("#dot").className = "dot" + (on && !semDados ? " on" : "");
  $("#statusTxt").textContent = on
    ? semDados
      ? "Online · sem leituras"
      : "Online · " + fmtHm(s.ultima_visto)
    : s && s.ultima_visto
      ? "Offline"
      : "Sem dados";
  const off = s && !s.online && s.ultima_visto;
  $("#offBanner").hidden = !(off || semDados);
  if (off) {
    $("#offTxt").textContent =
      `ESP32 sem contato desde ${fmtHora(s.ultima_visto)}. ` +
      "As leituras abaixo são as últimas recebidas.";
  } else if (semDados) {
    $("#offTxt").textContent =
      `O ESP32 está conectado, mas não envia leituras desde ${fmtHora(s.ultima_telemetria)} ` +
      `(há ${fmtDur(parado)}).`;
  }
}
function pintarUltima(t) {
  if (!t) return;
  ultimaTel = t;
  $("#temp").textContent = t.temp != null ? Number(t.temp).toFixed(1) : "--";
  $("#u1").textContent = fmtNum(t.u1);
  $("#u2").textContent = fmtNum(t.u2);
  $("#ucr1").textContent = fmtNum(t.ucorr1);
  $("#ucr2").textContent = fmtNum(t.ucorr2);
  $("#ultTs").textContent = "Última leitura " + fmtQuando(t.ts);
  pintarEscalas();
}
function pintarEscalas() {
  const t = ultimaTel;
  if (!t) return;
  const c = cfgAtual || {};
  const A = c.aquecedor || {};
  const V = c.ventilador || {};
  // temperatura: faixa entre "aquecedor liga" e "ventilador liga" (dia)
  const lo = A.liga_dia_c;
  const hi = V.liga_dia_c;
  const v = Number(t.temp);
  if (lo != null && hi != null && !isNaN(v)) {
    const a = Math.min(lo - 5, v - 1);
    const b = Math.max(hi + 3, v + 1);
    const P = (x) => (((x - a) / (b - a)) * 100).toFixed(1);
    const st =
      v < lo
        ? "Abaixo da faixa"
        : v > hi
          ? "Acima da faixa"
          : "Dentro da faixa";
    const largura = (P(hi) - P(lo)).toFixed(1);
    $("#tempScale").innerHTML = `
      <div class="sub" style="margin-top:6px">
        ${st} · aquece abaixo de ${lo} °C, ventila acima de ${hi} °C
      </div>
      <div class="scale">
        <div class="bar">
          <div class="band" style="left:${P(lo)}%;width:${largura}%"></div>
          <div class="pt" style="left:${P(v)}%"></div>
        </div>
        <div class="lbl">
          <span>${Math.round(a)} °C</span>
          <span>${Math.round(b)} °C</span>
        </div>
      </div>`;
  }
  // umidade: posição no intervalo das últimas 24 h (maior ADC = mais seco)
  const serie = cacheTel[24] && cacheTel[24].rows;
  ["u1", "u2"].forEach((f, i) => {
    const cur = Number(t[f]);
    const el = $(`#${f}Scale`);
    const vals = serie
      ? serie.map((r) => Number(r[f])).filter((x) => !isNaN(x))
      : [];
    if (!vals.length || isNaN(cur)) {
      el.innerHTML = "";
      return;
    }
    const mn = Math.min(...vals, cur);
    const mx = Math.max(...vals, cur);
    const pad = (mx - mn) * 0.2 || 10;
    const a = mn - pad;
    const b = mx + pad;
    const P = (x) => (((x - a) / (b - a)) * 100).toFixed(1);
    const largura = (P(mx) - P(mn)).toFixed(1);
    el.innerHTML = `
      <div class="scale">
        <div class="bar">
          <div class="rng" style="left:${P(mn)}%;width:${largura}%"></div>
          <div class="pt" style="left:${P(cur)}%"></div>
        </div>
        <div class="lbl">
          <span>úmido</span>
          <span title="Faixa das últimas 24 h">${Math.round(mn)}–${Math.round(mx)}</span>
          <span>seco</span>
        </div>
      </div>`;
  });
}
function barraFoto(on, off, comAgora) {
  if (on == null || off == null) return "";
  const W = 360;
  const X = (h) => 8 + (h / 24) * (W - 16);
  let s = `<svg class="ch" viewBox="0 0 ${W} 46" role="img"
    aria-label="Fotoperíodo das ${on} às ${off} h">`;
  s += `<rect x="8" y="8" width="${W - 16}" height="16" rx="4" fill="var(--off-bg)"/>`;
  const seg = (a, b) => {
    const largura = Math.max(0, X(b) - X(a));
    return `<rect x="${X(a)}" y="8" width="${largura}" height="16" rx="3" fill="var(--c-led)"/>`;
  };
  s += on < off ? seg(on, off) : seg(on, 24) + seg(0, off);
  if (comAgora) {
    const d = new Date();
    const x = X(d.getHours() + d.getMinutes() / 60);
    s += `<line x1="${x}" x2="${x}" y1="2" y2="30" stroke="var(--fg)" stroke-width="2"/>`;
  }
  for (const h of [0, 6, 12, 18, 24]) {
    const rotulo = String(h).padStart(2, "0") + "h";
    s += `<text x="${X(h)}" y="42" text-anchor="middle">${rotulo}</text>`;
  }
  return s + "</svg>";
}
function pintarFoto() {
  const F = (cfgAtual || {}).fotoperiodo || {};
  if (F.liga_h == null) {
    $("#fotoResumo").innerHTML =
      `<div class="sub">Fotoperíodo não configurado.</div>`;
    return;
  }
  const ligada = ultAtu.led === 1 || ultAtu.led === true;
  const d = new Date();
  const h = d.getHours() + d.getMinutes() / 60;
  const fimH = ligada ? F.desliga_h : F.liga_h;
  let falta = fimH - h;
  if (falta <= 0) falta += 24;
  const horasLuz = (F.desliga_h - F.liga_h + 24) % 24;
  const proxima = `${ligada ? "Desliga" : "Liga"} em ${fmtDur(falta * 3600000)}`;
  $("#fotoResumo").innerHTML = `
    <div class="val">${horasLuz}<small>h de luz por dia</small></div>
    <div class="sub" style="margin-top:6px">${proxima}</div>
    ${barraFoto(F.liga_h, F.desliga_h, true)}`;
}
