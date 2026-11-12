import logging
import time
from contextlib import asynccontextmanager

from fastapi import FastAPI
from sqlalchemy import text

from . import models  # noqa: F401  (registra as tabelas no metadata)
from . import alertas, mqtt_worker
from .api import router
from .config import settings
from .db import Base, engine

logging.basicConfig(level=logging.INFO)
log = logging.getLogger("app")


def _aguardar_db(tentativas: int = 30) -> None:
    for i in range(1, tentativas + 1):
        try:
            with engine.connect() as c:
                c.execute(text("SELECT 1"))
            return
        except Exception as e:  # noqa: BLE001
            log.warning("DB indisponivel (%s/%s): %s", i, tentativas, e)
            time.sleep(2)
    raise RuntimeError("Banco nao respondeu a tempo")


def _migrar() -> None:
    """Aplica migrations pendentes (Alembic). create_all garante as tabelas base
    para instalacoes novas; as migrations cuidam da evolucao do schema."""
    try:
        from alembic import command
        from alembic.config import Config
        cfg = Config("/app/alembic.ini")
        command.upgrade(cfg, "head")
        log.info("Migrations aplicadas (alembic upgrade head)")
    except Exception:
        # nao derruba o app; erro fica visivel para correcao manual
        log.exception("Falha ao aplicar migrations Alembic")


@asynccontextmanager
async def lifespan(app: FastAPI):
    _aguardar_db()
    Base.metadata.create_all(bind=engine)
    _migrar()
    mqtt_worker.iniciar()
    alertas.iniciar()
    log.info("Backend pronto (env=%s, device=%s)", settings.env, settings.device_id)
    yield


app = FastAPI(title="Estufa - Backend", lifespan=lifespan)
app.include_router(router)
