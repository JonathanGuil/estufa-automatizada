import logging
import threading
import time
from collections import defaultdict, deque
from datetime import datetime

from fastapi import APIRouter, Depends, Form, HTTPException, Query, Request, Response
from sqlalchemy import desc, select

from . import alertas, auth, conta, models, mqtt_worker
from .config import settings
from .db import SessionLocal
from .schemas import Comando, PedidoLink, Redefinicao, Setpoints, TrocaEmail, TrocaSenha

router = APIRouter(prefix="/api")
log = logging.getLogger("api")


def db():
    with SessionLocal() as s:
        yield s


def _tel(r: models.Telemetria | None):
    if r is None:
        return None
    return {
        "ts": r.ts, "temp": r.temp,
        "u1": r.u1, "u2": r.u2,
        "uctrl1": r.uctrl1, "uctrl2": r.uctrl2,
        "ucorr1": r.ucorr1, "ucorr2": r.ucorr2,
        "atuadores": r.atuadores, "origem": r.origem,
    }


@router.get("/health")
def health():
    return {"ok": True, "env": settings.env}


class _Limite:
    """Conta tentativas por chave (IP) numa janela de tempo."""

    def __init__(self, maximo: int, janela_s: int):
        self.maximo = maximo
        self.janela = janela_s
        self._ev: dict[str, deque] = defaultdict(deque)

    def espera(self, chave: str) -> int:
        """Segundos ate liberar a chave (0 = liberada)."""
        agora = time.monotonic()
        ev = self._ev[chave]
        while ev and agora - ev[0] > self.janela:
            ev.popleft()
        if len(ev) < self.maximo:
            return 0
        return int(self.janela - (agora - ev[0])) + 1

    def registrar(self, chave: str) -> None:
        agora = time.monotonic()
        self._ev[chave].append(agora)
        if len(self._ev) > 1000:   # limpeza simples de IPs antigos
            for k in [k for k, f in self._ev.items() if not f or agora - f[-1] > self.janela]:
                self._ev.pop(k, None)

    def limpar(self, chave: str) -> None:
        self._ev.pop(chave, None)


# Freios contra tentativa e erro (o painel fica exposto na internet): 5 senhas erradas do
# mesmo IP em 10 min (login, senha atual na Conta e link invalido) e 3 pedidos de
# "Esqueci minha senha" por IP em 15 min bloqueiam esse IP ate a janela passar.
_falhas_senha = _Limite(5, 600)
_pedidos_link = _Limite(3, 900)
_INTERVALO_LINK_S = 120   # no maximo um e-mail de redefinicao a cada 2 min
_INTERVALO_TESTE_S = 60   # e um e-mail de teste por minuto
_ultimo_link: float | None = None
_ultimo_teste: float | None = None


def _muitas_tentativas(espera: int) -> HTTPException:
    return HTTPException(
        status_code=429,
        detail=f"Muitas tentativas. Tente de novo em {espera // 60 + 1} min.",
        headers={"Retry-After": str(espera)},
    )


def _ip_cliente(request: Request) -> str:
    # Atras do Caddy: ele substitui o X-Forwarded-For pelo IP real do cliente.
    xff = request.headers.get("x-forwarded-for", "")
    if xff:
        return xff.split(",")[0].strip()
    return request.client.host if request.client else "?"


@router.post("/login")
def login(request: Request, resp: Response, usuario: str = Form(...), senha: str = Form(...)):
    ip = _ip_cliente(request)
    espera = _falhas_senha.espera(ip)
    if espera:
        raise _muitas_tentativas(espera)
    if not auth.conferir_credenciais(usuario, senha):
        _falhas_senha.registrar(ip)
        raise HTTPException(status_code=401, detail="Usuario ou senha invalidos")
    _falhas_senha.limpar(ip)
    auth.criar_sessao(resp, usuario)
    return {"ok": True, "usuario": usuario}


@router.post("/logout")
def logout(resp: Response):
    auth.encerrar_sessao(resp)
    return {"ok": True}


@router.get("/me")
def me(usuario: str = Depends(auth.require_login)):
    return {"usuario": usuario, "env": settings.env, "device_id": settings.device_id}


# ---------- Conta: senha e e-mail ----------

def _confirmar_senha(request: Request, senha: str) -> None:
    """Exige a senha atual, com o mesmo freio do login."""
    ip = _ip_cliente(request)
    espera = _falhas_senha.espera(ip)
    if espera:
        raise _muitas_tentativas(espera)
    if not conta.conferir_senha(senha):
        _falhas_senha.registrar(ip)
        raise HTTPException(status_code=403, detail="Senha atual incorreta.")


def _enviar_link(para: str) -> None:
    token = conta.criar_link()
    link = f"{settings.painel_url.rstrip('/')}/#redefinir={token}"
    corpo = (
        "Recebemos um pedido para redefinir a senha do painel da estufa.\n\n"
        "Para criar uma senha nova, abra o link abaixo. Ele vale por 30 minutos e "
        f"funciona uma vez só:\n\n{link}\n\n"
        "Se não foi você, ignore este e-mail: a senha atual continua valendo."
    )
    alertas.enviar("redefinição de senha", corpo, para=para)


@router.post("/senha/esqueci")
def senha_esqueci(dados: PedidoLink, request: Request):
    """Manda o link de redefinicao se o e-mail for o da conta. A resposta e a mesma nos
    dois casos, para nao revelar qual e o e-mail cadastrado."""
    global _ultimo_link
    ip = _ip_cliente(request)
    espera = _pedidos_link.espera(ip)
    if espera:
        raise _muitas_tentativas(espera)
    _pedidos_link.registrar(ip)
    if not (settings.smtp_host and settings.painel_url and conta.email()):
        raise HTTPException(status_code=503,
                            detail="A recuperação de senha por e-mail não está configurada neste ambiente.")
    para = conta.email_confere(dados.email)
    agora = time.monotonic()
    if para and (_ultimo_link is None or agora - _ultimo_link >= _INTERVALO_LINK_S):
        _ultimo_link = agora
        threading.Thread(target=_enviar_link, args=(para,), daemon=True).start()
    elif para:
        log.info("Pedido de redefinicao ignorado: houve outro ha menos de %s s", _INTERVALO_LINK_S)
    else:
        log.info("Pedido de redefinicao com um e-mail que nao e o da conta (IP %s)", ip)
    return {"ok": True}


@router.post("/senha/redefinir")
def senha_redefinir(dados: Redefinicao, request: Request):
    ip = _ip_cliente(request)
    espera = _falhas_senha.espera(ip)
    if espera:
        raise _muitas_tentativas(espera)
    erro = conta.validar_senha_nova(dados.senha)
    if erro:
        raise HTTPException(status_code=400, detail=erro)
    if not conta.redefinir_com_link(dados.token, dados.senha):
        _falhas_senha.registrar(ip)
        raise HTTPException(status_code=400,
                            detail="Este link não vale mais (passou de 30 minutos ou já foi usado). "
                                   "Peça outro em \"Esqueci minha senha\".")
    log.info("Senha redefinida pelo link enviado por e-mail")
    return {"ok": True}


@router.get("/conta")
def get_conta(_=Depends(auth.require_login)):
    d = conta.dados()
    d["envio_configurado"] = bool(settings.smtp_host)
    return d


@router.put("/conta/email")
def put_conta_email(dados: TrocaEmail, request: Request, _=Depends(auth.require_login)):
    endereco = conta.normalizar_email(dados.email)
    if not endereco:
        raise HTTPException(status_code=400, detail="Digite um e-mail válido (um endereço só).")
    _confirmar_senha(request, dados.senha_atual)
    conta.definir_email(endereco)
    log.info("E-mail da conta trocado pelo painel")
    return {"ok": True, "email": endereco}


@router.post("/conta/email/teste")
def post_conta_email_teste(_=Depends(auth.require_login)):
    global _ultimo_teste
    if not settings.smtp_host:
        raise HTTPException(status_code=503,
                            detail="O envio de e-mail não está configurado (falta o SMTP no .env da VM).")
    para = conta.email()
    if not para:
        raise HTTPException(status_code=409, detail="Salve um e-mail primeiro.")
    agora = time.monotonic()
    if _ultimo_teste is not None and agora - _ultimo_teste < _INTERVALO_TESTE_S:
        raise HTTPException(status_code=429, detail="Espere um minuto para mandar outro teste.")
    _ultimo_teste = agora
    if not alertas.enviar("teste de e-mail",
                          "Se este e-mail chegou, os alertas da estufa vão chegar aqui.", para=para):
        raise HTTPException(status_code=502,
                            detail="O servidor de e-mail recusou o envio. Veja o log do backend.")
    return {"ok": True, "para": para}


@router.post("/conta/senha")
def post_conta_senha(dados: TrocaSenha, request: Request, resp: Response,
                     usuario: str = Depends(auth.require_login)):
    erro = conta.validar_senha_nova(dados.nova)
    if erro:
        raise HTTPException(status_code=400, detail=erro)
    _confirmar_senha(request, dados.senha_atual)
    conta.definir_senha(dados.nova)
    auth.criar_sessao(resp, usuario)   # esta sessao continua; as outras sao encerradas
    log.info("Senha trocada pelo painel")
    return {"ok": True}


@router.get("/status")
def status(s=Depends(db), _=Depends(auth.require_login)):
    st = s.get(models.DispositivoStatus, settings.device_id)
    return {
        "device_id": settings.device_id,
        "online": bool(st.online) if st else False,
        "ultima_visto": st.ultima_visto if st else None,
        "ultima_telemetria": st.ultima_telemetria if st else None,
        "sem_dados_min": settings.alerta_sem_dados_min,
        "atuadores": mqtt_worker.estado_atuadores(),
    }


@router.get("/telemetria/ultima")
def ultima(s=Depends(db), _=Depends(auth.require_login)):
    r = s.execute(
        select(models.Telemetria)
        .where(models.Telemetria.device_id == settings.device_id)
        .order_by(desc(models.Telemetria.ts)).limit(1)
    ).scalar_one_or_none()
    return _tel(r)


@router.get("/telemetria")
def telemetria(
    s=Depends(db), _=Depends(auth.require_login),
    limit: int = Query(300, ge=1, le=20000),   # 30 dias a ~5 min/leitura cabem folgado
    desde: datetime | None = None, ate: datetime | None = None,
):
    q = select(models.Telemetria).where(models.Telemetria.device_id == settings.device_id)
    if desde:
        q = q.where(models.Telemetria.ts >= desde)
    if ate:
        q = q.where(models.Telemetria.ts <= ate)
    q = q.order_by(desc(models.Telemetria.ts)).limit(limit)
    rows = s.execute(q).scalars().all()
    return [_tel(r) for r in rows]


@router.get("/eventos")
def eventos(
    s=Depends(db), _=Depends(auth.require_login),
    limit: int = Query(100, ge=1, le=10000),
    desde: datetime | None = None, ate: datetime | None = None,
):
    q = select(models.Evento).where(models.Evento.device_id == settings.device_id)
    if desde:
        q = q.where(models.Evento.ts >= desde)
    if ate:
        q = q.where(models.Evento.ts <= ate)
    rows = s.execute(q.order_by(desc(models.Evento.ts)).limit(limit)).scalars().all()
    return [{"ts": r.ts, "tipo": r.tipo, "detalhe": r.detalhe, "origem": r.origem} for r in rows]


@router.get("/config")
def get_config(s=Depends(db), _=Depends(auth.require_login)):
    ativa = s.get(models.ConfigAtiva, settings.device_id)
    des = s.execute(
        select(models.ConfigDesejada)
        .where(models.ConfigDesejada.device_id == settings.device_id)
        .order_by(desc(models.ConfigDesejada.ts)).limit(1)
    ).scalar_one_or_none()
    return {
        "device_id": settings.device_id,
        "ativa": ativa.payload if ativa else None,
        "ativa_ts": ativa.ts if ativa else None,
        "desejada": des.payload if des else None,
        "desejada_ts": des.ts if des else None,
    }


@router.put("/config")
def put_config(sp: Setpoints, s=Depends(db), usuario: str = Depends(auth.require_login)):
    payload = sp.model_dump(exclude_none=True)
    s.add(models.ConfigDesejada(device_id=settings.device_id, payload=payload, aplicado_por=usuario))
    s.commit()
    topico = mqtt_worker.publicar_config(payload)
    return {"ok": True, "publicado_em": topico, "payload": payload}


@router.post("/comando")
def post_comando(cmd: Comando, usuario: str = Depends(auth.require_login)):
    """Acionamento remoto: muda o modo (auto/manual) e liga/desliga em manual.
    Publica em comando/set sem retain (comando nao e estado persistente). O ESP32
    aplica com as mesmas travas do painel local; o evento 'comando' (origem 'nuvem')
    que volta traz o novo estado e serve de trilha de auditoria."""
    payload = cmd.model_dump(exclude_none=True)
    if not payload:
        raise HTTPException(status_code=400, detail="Comando vazio")
    topico = mqtt_worker.publicar_comando(payload)
    return {"ok": True, "publicado_em": topico, "payload": payload}
