"""Assina o broker MQTT em background, grava telemetria/eventos/status no
Postgres (deduplicando por fingerprint) e publica setpoints quando o painel pede."""
import json
import logging
from datetime import datetime, timezone

import paho.mqtt.client as mqtt
from sqlalchemy.dialects.postgresql import insert as pg_insert

from . import alertas, models
from .config import settings
from .db import SessionLocal
from .dedup import fp_evento, fp_telemetria

log = logging.getLogger("mqtt")
_client: mqtt.Client | None = None

# Ids dos atuadores no firmware. O estado corrente fica em memoria e e atualizado
# tanto pela telemetria quanto pelos eventos (todo evento traz o retrato no 'detalhe'),
# para o painel refletir modo/estado em ~1 s em vez de esperar a telemetria periodica
# (~5 min). Comeca vazio a cada reinicio; a primeira mensagem preenche.
ATUADOR_IDS = ("vent", "aquec", "exaus", "led", "valv1", "valv2")
_estado_atuadores: dict | None = None


def _norm_atuadores(d) -> dict | None:
    """Extrai so os ids conhecidos (+ submapa 'manual') de telemetria.atuadores ou
    evento.detalhe. Retorna None se nao houver nenhum id conhecido (ignora payloads
    de teste com nomes antigos, ex.: {"ventilador": true})."""
    if not isinstance(d, dict):
        return None
    out = {k: d[k] for k in ATUADOR_IDS if k in d}
    if not out:
        return None
    man = d.get("manual")
    if isinstance(man, dict):
        out["manual"] = {k: man[k] for k in ATUADOR_IDS if k in man}
    return out


def _atualizar_estado(d) -> None:
    global _estado_atuadores
    at = _norm_atuadores(d)
    if at is not None:
        _estado_atuadores = at


def estado_atuadores() -> dict | None:
    return _estado_atuadores


def _topics() -> list[tuple[str, int]]:
    d = settings.device_id
    return [
        (f"estufa/{d}/telemetria", 1),
        (f"estufa/{d}/evento", 1),
        (f"estufa/{d}/online", 1),
        (f"estufa/{d}/config/state", 1),
    ]


def _parse_ts(v) -> datetime:
    if v is None:
        return datetime.now(timezone.utc)
    if isinstance(v, (int, float)):
        return datetime.fromtimestamp(v, tz=timezone.utc)
    try:
        return datetime.fromisoformat(str(v).replace("Z", "+00:00"))
    except ValueError:
        return datetime.now(timezone.utc)


def _tocar_status(s, *, visto=False, ts_tel=None) -> None:
    st = s.get(models.DispositivoStatus, settings.device_id)
    if st is None:
        st = models.DispositivoStatus(device_id=settings.device_id)
        s.add(st)
    agora = datetime.now(timezone.utc)
    if visto:
        st.ultima_visto = agora
        st.online = True
    # reenvio de backlog chega em ordem, mas nunca deixa a ultima leitura voltar no tempo
    if ts_tel is not None and (st.ultima_telemetria is None or ts_tel > st.ultima_telemetria):
        st.ultima_telemetria = ts_tel


def _salvar_telemetria(s, p: dict) -> None:
    ts = _parse_ts(p.get("ts"))
    epoch = int(ts.timestamp())
    fp = fp_telemetria(settings.device_id, epoch, p.get("temp"), p.get("u1"),
                       p.get("u2"), p.get("uctrl1"), p.get("uctrl2"), p.get("origem"))
    stmt = pg_insert(models.Telemetria).values(
        ts=ts, device_id=settings.device_id,
        temp=p.get("temp"), u1=p.get("u1"), u2=p.get("u2"),
        uctrl1=p.get("uctrl1"), uctrl2=p.get("uctrl2"),
        ucorr1=p.get("ucorr1"), ucorr2=p.get("ucorr2"),
        atuadores=p.get("atuadores"), origem=p.get("origem"), fp=fp,
    ).on_conflict_do_nothing(index_elements=["fp"])
    s.execute(stmt)
    _tocar_status(s, visto=True, ts_tel=ts)
    _atualizar_estado(p.get("atuadores"))


def _salvar_evento(s, p: dict) -> datetime | None:
    """Grava o evento; retorna o horario dele se for novo (None se ja existia)."""
    ts = _parse_ts(p.get("ts"))
    epoch = int(ts.timestamp())
    tipo = str(p.get("tipo", "desconhecido"))
    fp = fp_evento(settings.device_id, epoch, tipo, p.get("origem"))
    stmt = pg_insert(models.Evento).values(
        ts=ts, device_id=settings.device_id, tipo=tipo,
        detalhe=p.get("detalhe") if isinstance(p.get("detalhe"), dict) else p,
        origem=p.get("origem"), fp=fp,
    ).on_conflict_do_nothing(index_elements=["fp"])
    novo = s.execute(stmt.returning(models.Evento.id)).first() is not None
    _tocar_status(s, visto=True)
    _atualizar_estado(p.get("detalhe"))
    return ts if novo else None


def _salvar_online(s, texto: str) -> None:
    st = s.get(models.DispositivoStatus, settings.device_id)
    if st is None:
        st = models.DispositivoStatus(device_id=settings.device_id)
        s.add(st)
    st.online = texto.strip() in ("1", "online", "true", "True")
    st.ultima_visto = datetime.now(timezone.utc)


def _salvar_config_ativa(s, p: dict) -> None:
    ca = s.get(models.ConfigAtiva, settings.device_id)
    if ca is None:
        ca = models.ConfigAtiva(device_id=settings.device_id, payload=p)
        s.add(ca)
    else:
        ca.payload = p
        ca.ts = datetime.now(timezone.utc)
    _tocar_status(s, visto=True)


def on_connect(client, userdata, flags, reason_code, properties=None):
    if (reason_code.value if hasattr(reason_code, "value") else reason_code) == 0:
        log.info("MQTT conectado; assinando topicos de %s", settings.device_id)
        client.subscribe(_topics())
    else:
        log.error("Falha na conexao MQTT: %s", reason_code)


def on_message(client, userdata, msg):
    texto = msg.payload.decode("utf-8", "replace")
    try:
        payload = json.loads(texto)
    except ValueError:
        payload = {"raw": texto}
    tail = msg.topic.split("/")[-1]
    evento_novo = None
    try:
        with SessionLocal() as s:
            if tail == "telemetria":
                _salvar_telemetria(s, payload)
            elif tail == "evento":
                evento_novo = _salvar_evento(s, payload)
            elif tail == "online":
                _salvar_online(s, texto)
            elif tail == "state":       # .../config/state
                _salvar_config_ativa(s, payload)
            s.commit()
        if evento_novo is not None:
            alertas.evento_recebido(payload, evento_novo)
    except Exception:
        log.exception("Erro ao processar mensagem em %s", msg.topic)


def iniciar() -> mqtt.Client:
    global _client
    c = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"backend-{settings.env}-{settings.device_id}",
    )
    c.username_pw_set(settings.mqtt_user, settings.mqtt_pass)
    c.on_connect = on_connect
    c.on_message = on_message
    c.reconnect_delay_set(min_delay=1, max_delay=30)
    c.connect_async(settings.mqtt_host, settings.mqtt_port, keepalive=60)
    c.loop_start()
    _client = c
    log.info("Worker MQTT iniciado (%s:%s)", settings.mqtt_host, settings.mqtt_port)
    return c


def publicar_config(payload: dict) -> str:
    if _client is None:
        raise RuntimeError("MQTT nao iniciado")
    topico = f"estufa/{settings.device_id}/config/set"
    _client.publish(topico, json.dumps(payload), qos=1, retain=True)
    return topico


def publicar_comando(payload: dict) -> str:
    """Acionamento remoto. Publica sem retain de proposito: comando e uma acao
    pontual, nao um estado persistente. Se fosse retido, cada reconexao do ESP32
    reaplicaria o ultimo comando (ex.: reabrir valvula)."""
    if _client is None:
        raise RuntimeError("MQTT nao iniciado")
    topico = f"estufa/{settings.device_id}/comando/set"
    _client.publish(topico, json.dumps(payload), qos=1, retain=False)
    return topico
