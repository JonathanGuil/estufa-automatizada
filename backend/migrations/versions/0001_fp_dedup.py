"""fp dedup: coluna fp + backfill + indice unico em telemetria e evento

Revision ID: 0001_fp_dedup
Idempotente: seguro rodar em banco novo (fp ja criado pelo create_all) ou
antigo (fp ausente). Faz backfill das linhas existentes com a MESMA funcao de
fingerprint usada no ingest, remove duplicatas remanescentes e cria o indice
unico que sustenta o ON CONFLICT DO NOTHING.
"""
import sqlalchemy as sa
from alembic import op
from sqlalchemy import inspect

from app.dedup import fp_evento, fp_telemetria

revision = "0001_fp_dedup"
down_revision = None
branch_labels = None
depends_on = None


def _tem_coluna(insp, tabela, coluna):
    return coluna in [c["name"] for c in insp.get_columns(tabela)]


def _tem_indice(insp, tabela, indice):
    return indice in [i["name"] for i in insp.get_indexes(tabela)]


def upgrade():
    conn = op.get_bind()
    insp = inspect(conn)

    # 1) coluna fp (se ainda nao existir)
    if not _tem_coluna(insp, "telemetria", "fp"):
        op.add_column("telemetria", sa.Column("fp", sa.String(40), nullable=True))
    if not _tem_coluna(insp, "evento", "fp"):
        op.add_column("evento", sa.Column("fp", sa.String(40), nullable=True))

    # 2) backfill das linhas sem fp
    for r in conn.execute(sa.text(
        "SELECT id, device_id, ts, temp, u1, u2, uctrl1, uctrl2, origem "
        "FROM telemetria WHERE fp IS NULL"
    )).fetchall():
        fp = fp_telemetria(r.device_id, int(r.ts.timestamp()), r.temp, r.u1,
                           r.u2, r.uctrl1, r.uctrl2, r.origem)
        conn.execute(sa.text("UPDATE telemetria SET fp=:fp WHERE id=:id"),
                     {"fp": fp, "id": r.id})

    for r in conn.execute(sa.text(
        "SELECT id, device_id, ts, tipo, origem FROM evento WHERE fp IS NULL"
    )).fetchall():
        fp = fp_evento(r.device_id, int(r.ts.timestamp()), r.tipo, r.origem)
        conn.execute(sa.text("UPDATE evento SET fp=:fp WHERE id=:id"),
                     {"fp": fp, "id": r.id})

    # 3) remove duplicatas remanescentes (mantem o menor id) antes do indice unico
    conn.execute(sa.text("DELETE FROM telemetria a USING telemetria b "
                         "WHERE a.fp = b.fp AND a.id > b.id"))
    conn.execute(sa.text("DELETE FROM evento a USING evento b "
                         "WHERE a.fp = b.fp AND a.id > b.id"))

    # 4) indice unico (se ainda nao existir)
    insp = inspect(conn)
    if not _tem_indice(insp, "telemetria", "uq_telemetria_fp"):
        op.create_index("uq_telemetria_fp", "telemetria", ["fp"], unique=True)
    if not _tem_indice(insp, "evento", "uq_evento_fp"):
        op.create_index("uq_evento_fp", "evento", ["fp"], unique=True)

    # 5) garante default de recebido_em no banco (inserts Core omitem a coluna).
    #    Em tabelas antigas o default so existia no lado Python (ORM); agora que
    #    o ingest usa Core (pg_insert), o default precisa estar no proprio banco.
    op.execute("ALTER TABLE telemetria ALTER COLUMN recebido_em SET DEFAULT now()")
    op.execute("ALTER TABLE evento ALTER COLUMN recebido_em SET DEFAULT now()")


def downgrade():
    op.drop_index("uq_telemetria_fp", table_name="telemetria")
    op.drop_index("uq_evento_fp", table_name="evento")
    op.drop_column("telemetria", "fp")
    op.drop_column("evento", "fp")
