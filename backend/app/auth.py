import hmac

from fastapi import HTTPException, Request, Response
from itsdangerous import BadSignature, SignatureExpired, URLSafeTimedSerializer

from . import conta
from .config import settings

_serializer = URLSafeTimedSerializer(settings.session_secret, salt="sessao-estufa")
COOKIE = "estufa_sessao"


def conferir_credenciais(usuario: str, senha: str) -> bool:
    ok_u = hmac.compare_digest(usuario.encode(), settings.auth_user.encode())
    ok_p = conta.conferir_senha(senha)
    return ok_u and ok_p


def criar_sessao(resp: Response, usuario: str) -> None:
    # "v" e a versao da senha: quando a senha muda, as sessoes antigas deixam de valer
    token = _serializer.dumps({"u": usuario, "v": conta.versao_sessao()})
    resp.set_cookie(
        COOKIE, token,
        httponly=True, samesite="lax", secure=True,
        max_age=settings.session_max_age, path="/",
    )


def encerrar_sessao(resp: Response) -> None:
    resp.delete_cookie(COOKIE, path="/")


def require_login(request: Request) -> str:
    token = request.cookies.get(COOKIE)
    if not token:
        raise HTTPException(status_code=401, detail="Nao autenticado")
    try:
        dados = _serializer.loads(token, max_age=settings.session_max_age)
    except (BadSignature, SignatureExpired):
        raise HTTPException(status_code=401, detail="Sessao invalida ou expirada")
    if dados.get("v", 0) != conta.versao_sessao():
        raise HTTPException(status_code=401, detail="Sessao encerrada pela troca de senha")
    return dados.get("u", "")
