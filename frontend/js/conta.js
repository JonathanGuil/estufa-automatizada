// Conta: "Esqueci minha senha", senha nova pelo link do e-mail e a seção Conta do painel.
const CARTOES_LOGIN = ["loginForm", "esqueciForm", "redefinirForm"];
let tokenRedefinir = "";

function mostrarCartaoLogin(id) {
  CARTOES_LOGIN.forEach((c) => {
    $("#" + c).hidden = c !== id;
  });
}

function avisoLogin(el, texto, ok) {
  el.className = ok ? "aviso" : "erro";
  el.textContent = texto;
}

function msgConta(el, texto, tipo) {
  el.className = tipo ? "msg " + tipo : "msg";
  el.textContent = texto;
}

async function lerErro(r, padrao) {
  try {
    const d = await r.json();
    if (typeof d.detail === "string") return d.detail;
  } catch (e) {}
  return padrao;
}

const enviarJson = (caminho, dados, metodo = "POST") =>
  api(caminho, {
    method: metodo,
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(dados),
  });

// "Esqueci minha senha": pede o link por e-mail
$("#esqueciLink").onclick = () => {
  avisoLogin($("#esqMsg"), "", false);
  mostrarCartaoLogin("esqueciForm");
  $("#esqEmail").focus();
};
$$("[data-voltar-login]").forEach((b) => {
  b.onclick = () => {
    mostrarCartaoLogin("loginForm");
    $("#u").focus();
  };
});

$("#esqueciForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  const msg = $("#esqMsg");
  const email = $("#esqEmail").value.trim();
  if (!email) {
    avisoLogin(msg, "Digite o e-mail.", false);
    return;
  }
  $("#esqEnviar").disabled = true;
  avisoLogin(msg, "Enviando…", true);
  try {
    const r = await enviarJson("/senha/esqueci", { email });
    if (r.ok) {
      avisoLogin(
        msg,
        "Se esse for o e-mail da conta, o link chega em instantes. " +
          "Ele vale por 30 minutos; confira também a caixa de spam.",
        true,
      );
    } else {
      avisoLogin(
        msg,
        await lerErro(r, "Não foi possível enviar agora."),
        false,
      );
    }
  } catch (err) {
    avisoLogin(msg, "Sem conexão com o servidor.", false);
  }
  $("#esqEnviar").disabled = false;
});

// Link do e-mail: o painel abre com #redefinir=<token>
function abrirRedefinir() {
  tokenRedefinir = location.hash.slice("#redefinir=".length);
  // tira o token da barra de endereço e do histórico
  history.replaceState(null, "", location.pathname + location.search);
  mostrarLogin();
  avisoLogin($("#redMsg"), "", false);
  mostrarCartaoLogin("redefinirForm");
  $("#redNova").focus();
}

$("#redefinirForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  const msg = $("#redMsg");
  const nova = $("#redNova").value;
  if (nova.length < 8) {
    avisoLogin(msg, "A senha precisa ter pelo menos 8 caracteres.", false);
    return;
  }
  if (nova !== $("#redNova2").value) {
    avisoLogin(msg, "As duas senhas não são iguais.", false);
    return;
  }
  $("#redSalvar").disabled = true;
  try {
    const r = await enviarJson("/senha/redefinir", {
      token: tokenRedefinir,
      senha: nova,
    });
    if (r.ok) {
      tokenRedefinir = "";
      $("#redNova").value = "";
      $("#redNova2").value = "";
      mostrarCartaoLogin("loginForm");
      $("#loginAviso").textContent = "Senha nova salva. Entre com ela.";
      $("#loginAviso").hidden = false;
      $("#s").value = "";
      $("#u").focus();
    } else {
      const padrao =
        "Este link não vale mais. Peça outro em “Esqueci minha senha”.";
      avisoLogin(msg, await lerErro(r, padrao), false);
    }
  } catch (err) {
    avisoLogin(msg, "Sem conexão com o servidor.", false);
  }
  $("#redSalvar").disabled = false;
});

// Seção Conta (com login): e-mail dos alertas e troca de senha
async function carregarConta() {
  try {
    const r = await api("/conta");
    if (r.status === 401) {
      mostrarLogin();
      return;
    }
    if (!r.ok) return;
    const c = await r.json();
    $("#ctEmail").value = c.email || "";
    $("#ctUsuario").textContent = "Usuário: " + c.usuario;
    $("#ctSemEnvio").hidden = c.envio_configurado;
    $("#ctTeste").disabled = !c.envio_configurado || !c.email;
  } catch (e) {}
}

$("#formEmail").addEventListener("submit", async (e) => {
  e.preventDefault();
  const msg = $("#ctEmailMsg");
  const email = $("#ctEmail").value.trim();
  const senha = $("#ctEmailSenha").value;
  if (!email || !senha) {
    msgConta(msg, "Preencha o e-mail e a senha atual.", "err");
    return;
  }
  msgConta(msg, "Salvando…", "");
  try {
    const r = await enviarJson(
      "/conta/email",
      { email, senha_atual: senha },
      "PUT",
    );
    if (r.status === 401) {
      mostrarLogin();
      return;
    }
    if (r.ok) {
      $("#ctEmailSenha").value = "";
      msgConta(msg, "E-mail salvo.", "ok");
      carregarConta();
    } else {
      msgConta(msg, await lerErro(r, "Não foi possível salvar."), "err");
    }
  } catch (err) {
    msgConta(msg, "Sem conexão com o servidor.", "err");
  }
});

$("#ctTeste").onclick = async () => {
  const msg = $("#ctEmailMsg");
  msgConta(msg, "Enviando…", "");
  try {
    const r = await api("/conta/email/teste", { method: "POST" });
    if (r.status === 401) {
      mostrarLogin();
      return;
    }
    if (r.ok) {
      const d = await r.json();
      msgConta(msg, `E-mail de teste enviado para ${d.para}.`, "ok");
    } else {
      msgConta(msg, await lerErro(r, "Não foi possível enviar."), "err");
    }
  } catch (err) {
    msgConta(msg, "Sem conexão com o servidor.", "err");
  }
};

$("#formSenha").addEventListener("submit", async (e) => {
  e.preventDefault();
  const msg = $("#ctSenhaMsg");
  const atual = $("#ctAtual").value;
  const nova = $("#ctNova").value;
  if (!atual) {
    msgConta(msg, "Digite a senha atual.", "err");
    return;
  }
  if (nova.length < 8) {
    msgConta(msg, "A senha nova precisa ter pelo menos 8 caracteres.", "err");
    return;
  }
  if (nova !== $("#ctNova2").value) {
    msgConta(msg, "As duas senhas novas não são iguais.", "err");
    return;
  }
  msgConta(msg, "Salvando…", "");
  try {
    const r = await enviarJson("/conta/senha", { senha_atual: atual, nova });
    if (r.status === 401) {
      mostrarLogin();
      return;
    }
    if (r.ok) {
      ["#ctAtual", "#ctNova", "#ctNova2"].forEach((id) => {
        $(id).value = "";
      });
      msgConta(msg, "Senha trocada. As outras sessões foram encerradas.", "ok");
    } else {
      msgConta(
        msg,
        await lerErro(r, "Não foi possível trocar a senha."),
        "err",
      );
    }
  } catch (err) {
    msgConta(msg, "Sem conexão com o servidor.", "err");
  }
});
