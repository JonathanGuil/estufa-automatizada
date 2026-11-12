let cfgDirty = false;
let cfgAtual = null;
const g = (id) => {
  const v = $(id).value;
  return v === "" ? undefined : Number(v);
};
const arrAt = (x, i, d) =>
  Array.isArray(x) ? x[i] : x !== undefined && x !== null ? x : d;
function preencherConfig(c) {
  const cfg = c.ativa || c.desejada || {};
  cfgAtual = cfg;
  const set = (id, v) => {
    if (v !== undefined && v !== null) $(id).value = v;
  };
  const A = cfg.aquecedor || {};
  const V = cfg.ventilador || {};
  const E = cfg.exaustor || {};
  const F = cfg.fotoperiodo || {};
  set("#aq_ld", A.liga_dia_c);
  set("#aq_dd", A.desliga_dia_c);
  set("#aq_ln", A.liga_noite_c);
  set("#aq_dn", A.desliga_noite_c);
  set("#ve_ld", V.liga_dia_c);
  set("#ve_dd", V.desliga_dia_c);
  set("#ve_ln", V.liga_noite_c);
  set("#ve_dn", V.desliga_noite_c);
  set("#ex_ld", E.liga_dia_c);
  set("#ex_dd", E.desliga_dia_c);
  set("#ex_ln", E.liga_noite_c);
  set("#ex_dn", E.desliga_noite_c);
  set("#fp_on", F.liga_h);
  set("#fp_off", F.desliga_h);
  const CI = cfg.circulacao || {};
  $("#ci_en").checked = CI.habilitado !== false;
  set("#ci_ini", CI.hora_ini_h);
  set("#ci_fim", CI.hora_fim_h);
  set("#ci_on", CI.liga_min);
  set("#ci_off", CI.desliga_min);
  $("#ci_ex").checked = !!CI.aciona_exaustor;
  const ir = cfg.irrigacao || {};
  set("#ir_dose1", arrAt(ir.dose_ml, 0));
  set("#ir_dose2", arrAt(ir.dose_ml, 1));
  set("#ir_int1", arrAt(ir.intervalo_h, 0));
  set("#ir_int2", arrAt(ir.intervalo_h, 1));
  set("#z1_v", arrAt(ir.vazao_ml_min, 0));
  set("#z2_v", arrAt(ir.vazao_ml_min, 1));
  set("#z1_sat", arrAt(ir.limiar_saturado_adc, 0));
  set("#z2_sat", arrAt(ir.limiar_saturado_adc, 1));
  $("#ir_superv").checked = ir.supervisao_umidade !== false;
  $("#ir_reinicia").checked = !!ir.dose_manual_reinicia_intervalo;
  const pendente =
    c.desejada_ts &&
    c.ativa_ts &&
    new Date(c.desejada_ts) > new Date(c.ativa_ts);
  const meta = pendente
    ? `Envio de ${fmtQuando(c.desejada_ts)} aguardando confirmação do ESP32`
    : c.ativa_ts
      ? `Em vigor no ESP32 desde ${fmtQuando(c.ativa_ts)}`
      : "Nenhuma configuração registrada ainda";
  $$("[data-cfgmeta]").forEach((el) => {
    el.textContent = meta;
  });
  cfgDirty = false;
  pintarFotoPreview();
  pintarFoto();
  pintarEscalas();
  pintarAtuadores(ultAtu);
}
function objNum(pares) {
  const o = {};
  for (const [sel, key] of pares) {
    const v = g(sel);
    if (v !== undefined) o[key] = v;
  }
  return Object.keys(o).length ? o : undefined;
}
function parNum(a, b) {
  const x = g(a);
  const y = g(b);
  return x !== undefined && y !== undefined ? [x, y] : undefined;
}
function montarPayload() {
  const p = { versao: 1 };
  const fp = objNum([
    ["#fp_on", "liga_h"],
    ["#fp_off", "desliga_h"],
  ]);
  if (fp) p.fotoperiodo = fp;
  const aq = objNum([
    ["#aq_ld", "liga_dia_c"],
    ["#aq_dd", "desliga_dia_c"],
    ["#aq_ln", "liga_noite_c"],
    ["#aq_dn", "desliga_noite_c"],
  ]);
  if (aq) p.aquecedor = aq;
  const ve = objNum([
    ["#ve_ld", "liga_dia_c"],
    ["#ve_dd", "desliga_dia_c"],
    ["#ve_ln", "liga_noite_c"],
    ["#ve_dn", "desliga_noite_c"],
  ]);
  if (ve) p.ventilador = ve;
  const ex = objNum([
    ["#ex_ld", "liga_dia_c"],
    ["#ex_dd", "desliga_dia_c"],
    ["#ex_ln", "liga_noite_c"],
    ["#ex_dn", "desliga_noite_c"],
  ]);
  if (ex) p.exaustor = ex;
  const ci = {
    habilitado: $("#ci_en").checked,
    aciona_exaustor: $("#ci_ex").checked,
  };
  const ciIni = g("#ci_ini");
  if (ciIni !== undefined) ci.hora_ini_h = ciIni;
  const ciFim = g("#ci_fim");
  if (ciFim !== undefined) ci.hora_fim_h = ciFim;
  const ciOn = g("#ci_on");
  if (ciOn !== undefined) ci.liga_min = ciOn;
  const ciOff = g("#ci_off");
  if (ciOff !== undefined) ci.desliga_min = ciOff;
  p.circulacao = ci;
  const ir = {};
  const d = parNum("#ir_dose1", "#ir_dose2");
  if (d) ir.dose_ml = d;
  const it = parNum("#ir_int1", "#ir_int2");
  if (it) ir.intervalo_h = it;
  const vz = parNum("#z1_v", "#z2_v");
  if (vz) ir.vazao_ml_min = vz;
  const st = parNum("#z1_sat", "#z2_sat");
  if (st) ir.limiar_saturado_adc = st;
  ir.supervisao_umidade = $("#ir_superv").checked;
  ir.dose_manual_reinicia_intervalo = $("#ir_reinicia").checked;
  p.irrigacao = ir;
  return p;
}
// validação simples de coerência antes de enviar
function validar() {
  const erros = [];
  const par = (a, b, nome, abaixo) => {
    const x = g(a);
    const y = g(b);
    if (x === undefined || y === undefined) return;
    if (abaixo ? y < x : y > x)
      erros.push(
        `${nome}: o valor de desligar não pode ser ${abaixo ? "menor" : "maior"} que o de ligar.`,
      );
  };
  par("#aq_ld", "#aq_dd", "Aquecedor (dia)", true);
  par("#aq_ln", "#aq_dn", "Aquecedor (noite)", true);
  par("#ve_ld", "#ve_dd", "Ventilador (dia)", false);
  par("#ve_ln", "#ve_dn", "Ventilador (noite)", false);
  par("#ex_ld", "#ex_dd", "Exaustor (dia)", false);
  par("#ex_ln", "#ex_dn", "Exaustor (noite)", false);
  ["#fp_on", "#fp_off", "#ci_ini"].forEach((id) => {
    const v = g(id);
    if (v !== undefined && (v < 0 || v > 23))
      erros.push("Horas devem ficar entre 0 e 23.");
  });
  return [...new Set(erros)];
}
async function salvar(btn) {
  const msg = btn.parentElement.querySelector("[data-cfgmsg]");
  const erros = validar();
  if (erros.length) {
    msg.className = "msg err";
    msg.textContent = erros[0];
    return;
  }
  btn.disabled = true;
  msg.className = "msg";
  msg.textContent = "Enviando…";
  try {
    const r = await api("/config", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(montarPayload()),
    });
    if (r.ok) {
      msg.className = "msg ok";
      msg.textContent =
        "Enviado. O ESP32 aplica e confirma em alguns segundos.";
      cfgDirty = false;
      setTimeout(carregarConfig, 1500);
    } else if (r.status === 401) {
      mostrarLogin();
    } else {
      msg.className = "msg err";
      msg.textContent =
        `Não foi possível salvar (erro ${r.status}). ` +
        "Confira os valores e tente de novo.";
    }
  } catch (e) {
    msg.className = "msg err";
    msg.textContent = "Sem conexão com o servidor. Tente de novo.";
  }
  btn.disabled = false;
}
$$("[data-salvar]").forEach((b) => {
  b.onclick = () => salvar(b);
});
document.addEventListener("input", (e) => {
  const naConta = e.target.closest("#view-conta");
  if (e.target.closest("main") && !naConta && e.target.matches("input")) {
    cfgDirty = true;
    if (e.target.id === "fp_on" || e.target.id === "fp_off")
      pintarFotoPreview();
  }
});
function pintarFotoPreview() {
  const on = g("#fp_on");
  const off = g("#fp_off");
  $("#fotoPreview").innerHTML = barraFoto(on, off, false);
}
async function carregarConfig() {
  try {
    const r = await api("/config");
    if (r.ok) preencherConfig(await r.json());
  } catch (e) {}
}
