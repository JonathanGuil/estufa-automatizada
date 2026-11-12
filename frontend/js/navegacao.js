const VIEWS = ["geral", "clima", "irrigacao", "luz", "registro", "conta"];
let viewAtual = "geral";
function mostrarView(nome) {
  if (!VIEWS.includes(nome)) nome = "geral";
  viewAtual = nome;
  VIEWS.forEach((v) => {
    $("#view-" + v).hidden = v !== nome;
  });
  $$(".rail nav button").forEach((b) =>
    b.setAttribute("aria-current", b.dataset.view === nome ? "page" : "false"),
  );
  $("#viewTitle").textContent = $("#view-" + nome).dataset.title;
  if (location.hash.slice(1) !== nome)
    history.replaceState(null, "", "#" + nome);
  pintarPeriodos();
  desenhar(nome);
  if (["clima", "irrigacao", "luz"].includes(nome) && !cfgDirty)
    carregarConfig();
  if (nome === "conta") carregarConta();
}
$$(".rail nav button").forEach((b) => {
  b.onclick = () => mostrarView(b.dataset.view);
});
document.addEventListener("click", (e) => {
  const b = e.target.closest("[data-go]");
  if (b) mostrarView(b.dataset.go);
});
window.addEventListener("hashchange", () => {
  if (location.hash.startsWith("#redefinir=")) abrirRedefinir();
  else mostrarView(location.hash.slice(1));
});
function pintarPeriodos() {
  $$("[data-periodo]").forEach((el) => {
    el.innerHTML = [
      [24, "24 h"],
      [168, "7 dias"],
      [720, "30 dias"],
    ]
      .map(
        ([h, l]) =>
          `<button aria-pressed="${h === periodo}" data-per="${h}">${l}</button>`,
      )
      .join("");
  });
}
document.addEventListener("click", (e) => {
  const b = e.target.closest("[data-per]");
  if (!b) return;
  periodo = Number(b.dataset.per);
  pintarPeriodos();
  desenhar(viewAtual);
});
