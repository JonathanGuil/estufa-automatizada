"""Impressao digital (fingerprint) estavel de cada linha, para deduplicacao
idempotente. E calculada de forma IDENTICA no ingest MQTT e na importacao de
CSV, para que a mesma linha logica vinda de qualquer caminho colida no banco
(indice unico) e nunca duplique. Fonte unica de verdade para as duas pontas."""
import hashlib


def _s(v) -> str:
    return "" if v is None else str(v)


def _t(v) -> str:
    # temperatura sempre com 2 casas (igual ao String(x,2) da firmware e ao CSV)
    return "" if v is None else f"{float(v):.2f}"


def fp_telemetria(device_id, epoch, temp, u1, u2, uctrl1, uctrl2, origem) -> str:
    raw = f"{device_id}|T|{epoch}|{_s(origem)}|{_t(temp)}|{_s(u1)}|{_s(u2)}|{_s(uctrl1)}|{_s(uctrl2)}"
    return hashlib.sha1(raw.encode("utf-8")).hexdigest()


def fp_evento(device_id, epoch, tipo, origem) -> str:
    raw = f"{device_id}|E|{epoch}|{_s(tipo)}|{_s(origem)}"
    return hashlib.sha1(raw.encode("utf-8")).hexdigest()
