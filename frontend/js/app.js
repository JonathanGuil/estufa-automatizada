async function tentarLogin() {
  $("#loginErro").textContent = "";
  if (!$("#u").value || !$("#s").value) {
    $("#loginErro").textContent = "Preencha usuário e senha.";
    return;
  }
  const body = new URLSearchParams({
    usuario: $("#u").value,
    senha: $("#s").value,
  });
  try {
    const r = await api("/login", { method: "POST", body });
    if (r.ok) {
      $("#s").value = "";
      mostrarApp();
      iniciar();
    } else if (r.status === 429) {
      let m = "Muitas tentativas. Aguarde alguns minutos.";
      try {
        m = (await r.json()).detail || m;
      } catch (e) {}
      $("#loginErro").textContent = m;
    } else {
      $("#loginErro").textContent = "Usuário ou senha incorretos.";
    }
  } catch (e) {
    $("#loginErro").textContent = "Sem conexão com o servidor.";
  }
}
$("#loginForm").addEventListener("submit", (e) => {
  e.preventDefault();
  tentarLogin();
});
function mostrarApp() {
  $("#login").hidden = true;
  $("#app").hidden = false;
}
function mostrarLogin() {
  $("#app").hidden = true;
  $("#login").hidden = false;
  $("#loginAviso").hidden = true;
  mostrarCartaoLogin("loginForm");
  if (timer) {
    clearInterval(timer);
    timer = null;
  }
}
$("#sair").onclick = async () => {
  try {
    await api("/logout", { method: "POST" });
  } catch (e) {}
  mostrarLogin();
};

async function atualizar() {
  try {
    const [st, ult] = await Promise.all([
      api("/status"),
      api("/telemetria/ultima"),
    ]);
    if (st.status === 401) {
      mostrarLogin();
      return;
    }
    const s = await st.json();
    const t = ult.ok ? await ult.json() : null;
    pintarStatus(s);
    pintarUltima(t);
    ultAtu = s.atuadores || (t && t.atuadores) || {};
    pintarAtuadores(ultAtu);
    pintarFoto();
    pintarAlertas();
  } catch (e) {
    /* rede: mantém a tela anterior */
  }
}
let timer = null;
let ciclo = 0;
async function iniciar() {
  const me = await api("/me");
  if (!me.ok) {
    mostrarLogin();
    return;
  }
  try {
    $("#envTag").textContent = (await me.json()).env || "—";
  } catch (e) {}
  $("#legGantt").innerHTML = legendaGantt();
  $("#legReg").innerHTML = legendaGantt();
  await carregarConfig();
  eventos24 = await carregarEv(24);
  await atualizar();
  mostrarView(location.hash.slice(1) || "geral");
  if (timer) clearInterval(timer);
  timer = setInterval(async () => {
    await atualizar();
    if (!cfgDirty && viewAtual !== "geral" && viewAtual !== "conta")
      await carregarConfig();
    if (++ciclo % 6 === 0) {
      // a cada ~1 min, gráficos e eventos
      delete cacheTel[24];
      delete cacheEv[24];
      delete cacheTel[periodo];
      delete cacheEv[periodo];
      desenhar(viewAtual);
      if (viewAtual !== "geral")
        carregarEv(24).then((ev) => {
          eventos24 = ev;
          pintarAtuadores(ultAtu);
        });
    }
  }, 10000);
}
(async () => {
  if (location.hash.startsWith("#redefinir=")) {
    abrirRedefinir();
    return;
  }
  try {
    const r = await api("/me");
    if (r.ok) {
      mostrarApp();
      iniciar();
      return;
    }
  } catch (e) {}
  mostrarLogin();
})();
