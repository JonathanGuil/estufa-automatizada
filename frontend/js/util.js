// Funções comuns do painel: seletores, chamada à API e formatação de datas e durações.
const $ = (s) => document.querySelector(s);
const $$ = (s) => [...document.querySelectorAll(s)];
const api = (p, o = {}) =>
  fetch("/api" + p, { credentials: "same-origin", ...o });
const ENTIDADES_HTML = {
  "&": "&amp;",
  "<": "&lt;",
  ">": "&gt;",
  '"': "&quot;",
  "'": "&#39;",
};
const esc = (v) =>
  String(v ?? "").replace(/[&<>"']/g, (c) => ENTIDADES_HTML[c]);
const fmtNum = (v) => (v === null || v === undefined || v === "" ? "--" : v);
const fmtHora = (ts) =>
  ts
    ? new Date(ts).toLocaleString("pt-BR", {
        day: "2-digit",
        month: "2-digit",
        hour: "2-digit",
        minute: "2-digit",
      })
    : "—";
const fmtHm = (ts) =>
  ts
    ? new Date(ts).toLocaleTimeString("pt-BR", {
        hour: "2-digit",
        minute: "2-digit",
      })
    : "—";
function fmtQuando(ts) {
  if (!ts) return "—";
  const d = new Date(ts);
  const hoje = new Date();
  const ontem = new Date(Date.now() - 86400000);
  if (d.toDateString() === hoje.toDateString()) return fmtHm(ts);
  if (d.toDateString() === ontem.toDateString()) return "ontem " + fmtHm(ts);
  return fmtHora(ts);
}
function fmtDur(ms) {
  const m = Math.round(ms / 60000);
  if (m < 60) return m + " min";
  const h = Math.floor(m / 60);
  const r = m % 60;
  return r ? `${h} h ${r} min` : `${h} h`;
}
