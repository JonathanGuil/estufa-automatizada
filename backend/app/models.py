from datetime import datetime, timezone

from sqlalchemy import (BigInteger, Boolean, DateTime, Float, Index, Integer,
                        String, func)
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.orm import Mapped, mapped_column

from .db import Base


def _now() -> datetime:
    return datetime.now(timezone.utc)


class Telemetria(Base):
    __tablename__ = "telemetria"
    __table_args__ = (Index("uq_telemetria_fp", "fp", unique=True),)

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    ts: Mapped[datetime] = mapped_column(DateTime(timezone=True), index=True)
    device_id: Mapped[str] = mapped_column(String(64), index=True)
    temp: Mapped[float | None] = mapped_column(Float, nullable=True)
    u1: Mapped[int | None] = mapped_column(Integer, nullable=True)
    u2: Mapped[int | None] = mapped_column(Integer, nullable=True)
    uctrl1: Mapped[int | None] = mapped_column(Integer, nullable=True)
    uctrl2: Mapped[int | None] = mapped_column(Integer, nullable=True)
    ucorr1: Mapped[int | None] = mapped_column(Integer, nullable=True)
    ucorr2: Mapped[int | None] = mapped_column(Integer, nullable=True)
    atuadores: Mapped[dict | None] = mapped_column(JSONB, nullable=True)
    origem: Mapped[str | None] = mapped_column(String(32), nullable=True)
    fp: Mapped[str | None] = mapped_column(String(40), nullable=True)
    recebido_em: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())


class Evento(Base):
    __tablename__ = "evento"
    __table_args__ = (Index("uq_evento_fp", "fp", unique=True),)

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    ts: Mapped[datetime] = mapped_column(DateTime(timezone=True), index=True)
    device_id: Mapped[str] = mapped_column(String(64), index=True)
    tipo: Mapped[str] = mapped_column(String(64), index=True)
    detalhe: Mapped[dict | None] = mapped_column(JSONB, nullable=True)
    origem: Mapped[str | None] = mapped_column(String(32), nullable=True)
    fp: Mapped[str | None] = mapped_column(String(40), nullable=True)
    recebido_em: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())


class ConfigDesejada(Base):
    __tablename__ = "config_desejada"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String(64), index=True)
    ts: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=_now)
    payload: Mapped[dict] = mapped_column(JSONB)
    aplicado_por: Mapped[str | None] = mapped_column(String(64), nullable=True)


class ConfigAtiva(Base):
    __tablename__ = "config_ativa"

    device_id: Mapped[str] = mapped_column(String(64), primary_key=True)
    ts: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=_now)
    payload: Mapped[dict] = mapped_column(JSONB)


class DispositivoStatus(Base):
    __tablename__ = "dispositivo_status"

    device_id: Mapped[str] = mapped_column(String(64), primary_key=True)
    online: Mapped[bool] = mapped_column(Boolean, default=False)
    ultima_visto: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
    ultima_telemetria: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)


class Conta(Base):
    """Usuario do painel. Sem senha_hash vale o AUTH_PASS do .env; sem email, o ALERTA_EMAIL_PARA."""
    __tablename__ = "conta"

    usuario: Mapped[str] = mapped_column(String(64), primary_key=True)
    senha_hash: Mapped[str | None] = mapped_column(String(200), nullable=True)
    email: Mapped[str | None] = mapped_column(String(254), nullable=True)
    versao_sessao: Mapped[int] = mapped_column(Integer, default=0)
    alterado_em: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=_now)


class TokenSenha(Base):
    """Link de "Esqueci minha senha". Guarda so o hash do token enviado por e-mail."""
    __tablename__ = "token_senha"

    hash: Mapped[str] = mapped_column(String(64), primary_key=True)
    usuario: Mapped[str] = mapped_column(String(64))
    criado_em: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=_now)
    expira_em: Mapped[datetime] = mapped_column(DateTime(timezone=True))
    usado_em: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
