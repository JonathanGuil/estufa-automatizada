"""Conta do painel: senha, e-mail e link de "Esqueci minha senha".

Ha um usuario so (AUTH_USER). Enquanto nao houver senha gravada no banco, vale o AUTH_PASS
do .env; depois da primeira troca (pelo painel, pelo link enviado por e-mail ou com
python -m app.senha), vale a do banco. O e-mail da conta recebe os alertas e o link de
redefinicao; sem e-mail gravado, vale o ALERTA_EMAIL_PARA do .env.
"""
import base64
import hashlib
import hmac
import os
import re
import secrets
from datetime import datetime, timedelta, timezone

from sqlalchemy import delete

from . import models
from .config import settings
from .db import SessionLocal

SENHA_MIN = 8
SENHA_MAX = 128
VALIDADE_LINK = timedelta(minutes=30)
_EMAIL = re.compile(r"^[^@\s,;]+@[^@\s,;]+\.[^@\s,;]+$")

# scrypt com N=2^14, r=8, p=1: cerca de 16 MB e alguns centesimos de segundo por senha
_N, _R, _P = 2**14, 8, 1


def _agora() -> datetime:
    return datetime.now(timezone.utc)


def gerar_hash(senha: str) -> str:
    sal = os.urandom(16)
    h = hashlib.scrypt(senha.encode(), salt=sal, n=_N, r=_R, p=_P, dklen=32)
    sal64 = base64.b64encode(sal).decode()
    h64 = base64.b64encode(h).decode()
    return f"scrypt${_N}${_R}${_P}${sal64}${h64}"


def conferir_hash(senha: str, guardado: str) -> bool:
    try:
        alg, n, r, p, sal64, h64 = guardado.split("$")
        if alg != "scrypt":
            return False
        esperado = base64.b64decode(h64)
        calc = hashlib.scrypt(senha.encode(), salt=base64.b64decode(sal64),
                              n=int(n), r=int(r), p=int(p), dklen=len(esperado))
        return hmac.compare_digest(calc, esperado)
    except ValueError:
        return False


def _conta(s, criar: bool = False):
    c = s.get(models.Conta, settings.auth_user)
    if c is None and criar:
        c = models.Conta(usuario=settings.auth_user, versao_sessao=0)
        s.add(c)
    return c


def dados() -> dict:
    """Estado da conta para o painel (sem segredos)."""
    with SessionLocal() as s:
        c = _conta(s)
        email_banco = c.email if c and c.email else ""
        return {
            "usuario": settings.auth_user,
            "email": email_banco or settings.alerta_email_para.strip(),
            "email_no_painel": bool(email_banco),
            "senha_no_painel": bool(c and c.senha_hash),
        }


def email() -> str:
    """E-mail da conta: o salvo pelo painel ou, sem ele, o ALERTA_EMAIL_PARA do .env."""
    with SessionLocal() as s:
        c = _conta(s)
        if c and c.email:
            return c.email
    return settings.alerta_email_para.strip()


def versao_sessao() -> int:
    """Muda a cada troca de senha; sessoes com versao antiga deixam de valer."""
    with SessionLocal() as s:
        c = _conta(s)
        return c.versao_sessao if c else 0


def conferir_senha(senha: str) -> bool:
    with SessionLocal() as s:
        c = _conta(s)
        guardado = c.senha_hash if c else None
    if guardado:
        return conferir_hash(senha, guardado)
    return hmac.compare_digest(senha.encode(), settings.auth_pass.encode())


def validar_senha_nova(senha: str) -> str | None:
    """Mensagem para o painel se a senha nao serve; None se serve."""
    if len(senha) < SENHA_MIN:
        return f"A senha precisa ter pelo menos {SENHA_MIN} caracteres."
    if len(senha) > SENHA_MAX:
        return f"A senha pode ter no máximo {SENHA_MAX} caracteres."
    return None


def definir_senha(senha: str) -> None:
    """Grava a senha nova, encerra as sessoes abertas e invalida links pendentes."""
    h = gerar_hash(senha)
    with SessionLocal() as s:
        c = _conta(s, criar=True)
        c.senha_hash = h
        c.versao_sessao = (c.versao_sessao or 0) + 1
        c.alterado_em = _agora()
        s.execute(delete(models.TokenSenha).where(models.TokenSenha.usuario == settings.auth_user))
        s.commit()


def usar_senha_do_env() -> None:
    """Apaga a senha do banco: volta a valer o AUTH_PASS do .env (e encerra as sessoes)."""
    with SessionLocal() as s:
        c = _conta(s, criar=True)
        c.senha_hash = None
        c.versao_sessao = (c.versao_sessao or 0) + 1
        c.alterado_em = _agora()
        s.commit()


def normalizar_email(texto: str) -> str | None:
    """Endereco limpo, ou None se nao for um e-mail (um endereco so)."""
    e = (texto or "").strip()
    return e if len(e) <= 254 and _EMAIL.match(e) else None


def definir_email(endereco: str) -> None:
    with SessionLocal() as s:
        c = _conta(s, criar=True)
        c.email = endereco
        c.alterado_em = _agora()
        s.commit()


def email_confere(digitado: str) -> str | None:
    """Endereco da conta igual ao digitado (sem diferenciar maiusculas), ou None."""
    d = (digitado or "").strip().lower()
    if not d:
        return None
    for e in email().split(","):
        if e.strip() and e.strip().lower() == d:
            return e.strip()
    return None


def _hash_token(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


def criar_link() -> str:
    """Cria o token do link de redefinicao (so o ultimo pedido vale) e devolve o token."""
    token = secrets.token_urlsafe(32)
    with SessionLocal() as s:
        s.execute(delete(models.TokenSenha).where(models.TokenSenha.usuario == settings.auth_user))
        s.add(models.TokenSenha(hash=_hash_token(token), usuario=settings.auth_user,
                                expira_em=_agora() + VALIDADE_LINK))
        s.commit()
    return token


def redefinir_com_link(token: str, senha: str) -> bool:
    """Troca a senha se o link ainda vale (dentro do prazo e ainda nao usado)."""
    with SessionLocal() as s:
        t = s.get(models.TokenSenha, _hash_token(token), with_for_update=True)
        if t is None or t.usado_em is not None or t.expira_em < _agora():
            return False
        t.usado_em = _agora()
        s.commit()
    definir_senha(senha)
    return True
