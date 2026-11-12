const cacheTel = {};
const cacheEv = {};
// Pede uma lista grande; se o backend recusar o limite (422), repete com o limite menor.
async function buscarLista(caminho, limite, limiteAntigo) {
  try {
    let r = await api(`${caminho}&limit=${limite}`);
    if (r.status === 422) r = await api(`${caminho}&limit=${limiteAntigo}`);
    if (r.status === 401) {
      mostrarLogin();
      return [];
    }
    return r.ok ? await r.json() : [];
  } catch (e) {
    return [];
  }
}
async function carregarTel(horas, forcar) {
  const c = cacheTel[horas];
  if (c && !forcar && Date.now() - c.t < 60000) return c.rows;
  const desde = new Date(Date.now() - horas * 3600000).toISOString();
  const rows = (
    await buscarLista(
      `/telemetria?desde=${encodeURIComponent(desde)}`,
      20000,
      5000,
    )
  ).sort((a, b) => new Date(a.ts) - new Date(b.ts));
  cacheTel[horas] = { t: Date.now(), rows };
  return rows;
}
function parseDet(d) {
  if (!d) return {};
  if (typeof d === "object") return d;
  try {
    return JSON.parse(d);
  } catch (e) {
    return {};
  }
}
function normEvento(r) {
  const det = parseDet(r.detalhe);
  let estado = null;
  if (ATUADORES[r.tipo]) {
    const v = det[r.tipo] ?? det.estado ?? det.ligado ?? r.estado ?? r.valor;
    if (v === 1 || v === true || v === "1" || v === "on" || v === "ligado")
      estado = 1;
    else if (
      v === 0 ||
      v === false ||
      v === "0" ||
      v === "off" ||
      v === "desligado"
    )
      estado = 0;
  }
  return {
    ts: r.ts,
    t: new Date(r.ts).getTime(),
    tipo: r.tipo,
    origem: r.origem,
    estado,
    det,
  };
}
async function carregarEv(horas, forcar) {
  const c = cacheEv[horas];
  if (c && !forcar && Date.now() - c.t < 60000) return c.rows;
  const desde = new Date(Date.now() - horas * 3600000).toISOString();
  const t0 = Date.now() - horas * 3600000;
  const rows = (
    await buscarLista(
      `/eventos?desde=${encodeURIComponent(desde)}`,
      10000,
      2000,
    )
  )
    .map(normEvento)
    .filter((e) => e.t >= t0)
    .sort((a, b) => a.t - b.t);
  cacheEv[horas] = { t: Date.now(), rows };
  return rows;
}
// Intervalos em que o atuador ficou ligado, a partir das transições. No início da janela,
// o estado é o oposto da 1ª transição; no fim, vale o /status (mais confiável que o último
// evento). Evento sem campo de estado inverte o estado anterior.
function intervalos(k, evs, t0, t1) {
  const ts = evs.filter((e) => e.tipo === k);
  const out = [];
  if (!ts.length) {
    return ultAtu[k] === 1 || ultAtu[k] === true ? [[t0, t1]] : [];
  }
  let estado = ts[0].estado !== null ? 1 - ts[0].estado : 0;
  let ini = estado ? t0 : null;
  ts.forEach((e) => {
    const novo = e.estado !== null ? e.estado : 1 - estado;
    if (novo && !estado) {
      ini = e.t;
    }
    if (!novo && estado) {
      out.push([ini ?? t0, e.t]);
      ini = null;
    }
    estado = novo;
  });
  const agora = ultAtu[k] === 1 || ultAtu[k] === true;
  if (estado && ini != null)
    out.push([ini, agora ? t1 : Math.min(t1, ini + 60000)]);
  return out;
}
