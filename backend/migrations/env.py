"""Ambiente Alembic: usa a mesma engine/URL do app (por variavel DATABASE_URL),
entao cada ambiente (prod/hml) migra o proprio banco."""
from alembic import context

from app.db import Base, engine
from app import models  # noqa: F401  (registra as tabelas no metadata)

target_metadata = Base.metadata


def run_migrations_online() -> None:
    with engine.connect() as connection:
        context.configure(connection=connection, target_metadata=target_metadata)
        with context.begin_transaction():
            context.run_migrations()


run_migrations_online()
