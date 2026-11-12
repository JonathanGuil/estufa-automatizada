"""Alertas por e-mail: falhas do log local do ESP32, dose abandonada e falta de dados.

Os e-mails vao para o e-mail da conta, definido no painel (Conta); enquanto ele nao for
salvo, vale o ALERTA_EMAIL_PARA do .env. Sem SMTP_HOST, os alertas ficam so no log do
container (o painel mostra os mesmos eventos).

Teste de envio, dentro do container:  python -m app.alertas --teste
"""
import logging
import smtplib
import ssl
import sys
import threading
import time
from datetime import datetime, timedelta, timezone
from email.message import EmailMessage

from . import conta, models
from .config import settings
from .db import SessionLocal

log = logging.getLogger("alertas")

FUSO = timezone(timedelta(hours=-3))
REPETICAO = timedelta(hours=6)        # o mesmo tipo de alerta sai no maximo a cada 6 h
IDADE_MAXIMA = timedelta(hours=48)    # eventos mais antigos (ex.: reenvio de backlog) nao geram e-mail

TITULOS = {
    "log_cheio": "log local do ESP32 sem gravar",
    "log_falha": "falha ao apagar o log local",
    "log_descartado": "dados apagados antes de chegar à VM",
    "log_quase_cheio": "partição do ESP32 quase cheia",
    "dose_abandonada": "dose de irrigação não completada",
}

EVENTOS = {
    "log_cheio": "O ESP32 não consegue gravar no log local. As leituras seguem direto "
                 "para a VM; se a conexão cair, o que for medido nesse período se perde.",
    "log_falha": "O ESP32 não conseguiu apagar um arquivo antigo do log local.",
    "log_descartado": "O ESP32 apagou um dia do log local antes de ele chegar à VM.",
    "log_quase_cheio": "A partição do ESP32 passou de 85% de ocupação.",
    "dose_abandonada": "Uma dose de irrigação interrompida por reinício não foi completada "
                       "(já tinha sido retomada duas vezes).",
}

_ultimo_envio: dict[str, datetime] = {}
_trava = threading.Lock()
_sem_dados_avisado = False


def destinatario() -> str:
    """E-mail que recebe os alertas: o da conta (painel) ou o ALERTA_EMAIL_PARA do .env."""
    return conta.email()


def configurado() -> bool:
    return bool(settings.smtp_host and destinatario())


def _com_painel(corpo: str) -> str:
    return corpo + (f"\n\nPainel: {settings.painel_url}" if settings.painel_url else "")


def _hora(dt: datetime) -> str:
    return dt.astimezone(FUSO).strftime("%d/%m/%Y %H:%M")


def enviar(assunto: str, corpo: str, para: str | None = None) -> bool:
    """Envia pelo SMTP do .env. Sem 'para', vai para o e-mail da conta."""
    para = para or destinatario()
    if not (settings.smtp_host and para):
        log.warning("E-mail nao enviado (falta SMTP_HOST ou destinatario): %s", assunto)
        return False
    msg = EmailMessage()
    msg["Subject"] = f"[Estufa] {assunto}"
    msg["From"] = settings.alerta_email_de or settings.smtp_usuario
    msg["To"] = para
    msg.set_content(corpo)
    try:
        if settings.smtp_port == 465:
            with smtplib.SMTP_SSL(settings.smtp_host, settings.smtp_port, timeout=20,
                                  context=ssl.create_default_context()) as smtp:
                if settings.smtp_usuario:
                    smtp.login(settings.smtp_usuario, settings.smtp_senha)
                smtp.send_message(msg)
        else:
            with smtplib.SMTP(settings.smtp_host, settings.smtp_port, timeout=20) as smtp:
                smtp.starttls(context=ssl.create_default_context())
                if settings.smtp_usuario:
                    smtp.login(settings.smtp_usuario, settings.smtp_senha)
                smtp.send_message(msg)
        log.info("E-mail enviado: %s", assunto)
        return True
    except Exception:  # noqa: BLE001
        log.exception("Falha ao enviar o e-mail: %s", assunto)
        return False


def _enviar_em_segundo_plano(chave: str, assunto: str, corpo: str) -> None:
    """Respeita o intervalo minimo por tipo e nao bloqueia quem chamou."""
    agora = datetime.now(timezone.utc)
    with _trava:
        ultimo = _ultimo_envio.get(chave)
        if ultimo and agora - ultimo < REPETICAO:
            return
        _ultimo_envio[chave] = agora
    threading.Thread(target=enviar, args=(assunto, _com_painel(corpo)), daemon=True).start()


def evento_recebido(p: dict, ts: datetime) -> None:
    """Chamado pelo worker MQTT para cada evento novo gravado no banco."""
    tipo = str(p.get("tipo", ""))
    if tipo not in EVENTOS:
        return
    if datetime.now(timezone.utc) - ts > IDADE_MAXIMA:
        return
    det = p.get("detalhe") if isinstance(p.get("detalhe"), dict) else {}
    extras = []
    if det.get("arquivo"):
        extras.append(f"Arquivo: {det['arquivo']}")
    if det.get("uso_pct") is not None:
        extras.append(f"Ocupação: {det['uso_pct']}%")
    if det.get("zona") is not None:
        extras.append(f"Zona: {det['zona']}")
    if det.get("falta_s") is not None:
        extras.append(f"Faltaram: {det['falta_s']} s")
    corpo = f"{EVENTOS[tipo]}\n\nEvento: {tipo}\nHorário: {_hora(ts)}"
    if extras:
        corpo += "\n" + "\n".join(extras)
    _enviar_em_segundo_plano(tipo, f"{TITULOS[tipo]} ({_hora(ts)})", corpo)


def _verificar_sem_dados() -> None:
    """Avisa uma vez quando a telemetria para de chegar e outra quando volta."""
    global _sem_dados_avisado
    with SessionLocal() as s:
        st = s.get(models.DispositivoStatus, settings.device_id)
        ultima = st.ultima_telemetria if st else None
    if ultima is None:
        return
    limite = timedelta(minutes=settings.alerta_sem_dados_min)
    parado = datetime.now(timezone.utc) - ultima
    if parado > limite and not _sem_dados_avisado:
        _sem_dados_avisado = True
        threading.Thread(target=enviar, daemon=True, args=(
            f"sem dados desde {_hora(ultima)}",
            _com_painel(f"A VM não recebe leituras da estufa há {int(parado.total_seconds() // 60)} min "
                        f"(última leitura: {_hora(ultima)}). O ESP32 pode estar desligado, sem rede "
                        "ou sem conseguir publicar."))).start()
    elif parado <= limite and _sem_dados_avisado:
        _sem_dados_avisado = False
        threading.Thread(target=enviar, daemon=True, args=(
            "dados voltaram",
            _com_painel(f"As leituras voltaram a chegar (última: {_hora(ultima)})."))).start()


def _laco() -> None:
    while True:
        try:
            _verificar_sem_dados()
        except Exception:  # noqa: BLE001
            log.exception("Erro na verificacao de falta de dados")
        time.sleep(60)


def iniciar() -> None:
    threading.Thread(target=_laco, daemon=True, name="alertas").start()
    if configurado():
        log.info("Alertas por e-mail ligados (para %s)", destinatario())
    else:
        log.info("Alertas por e-mail desligados (falta SMTP_HOST ou o e-mail da conta)")


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    if "--teste" not in sys.argv:
        print("uso: python -m app.alertas --teste")
        sys.exit(2)
    if not configurado():
        print("Defina SMTP_HOST no .env e o e-mail da conta (painel > Conta, ou ALERTA_EMAIL_PARA).")
        sys.exit(1)
    ok = enviar("teste de alerta",
                _com_painel("Se este e-mail chegou, os alertas da estufa estão funcionando."))
    print("E-mail enviado." if ok else "Falhou; veja o erro acima.")
    sys.exit(0 if ok else 1)
