"""Importa o CSV de log do protótipo para o Postgres.

Cabeçalho esperado (o mesmo gravado pelo firmware; as colunas são lidas pelo nome):
  tipo,epoch,datahora,temp,umid1,umid2,uctrl1,uctrl2,ucorr1,ucorr2,
  vent,aquec,exaus,led,valv1,valv2,mvent,maquec,mexaus,mled,mvalv1,mvalv2,evento,origem

Roteamento pela coluna 'tipo':
  E        -> tabela 'evento'   (o tipo do evento vem da coluna 'evento')
  M / L    -> tabela 'telemetria'
'epoch' é UTC verdadeiro (conferido contra 'datahora'); usado direto, sem ajuste de fuso.
As colunas ucorr1/ucorr2 são opcionais: em logs antigos, sem elas, ficam NULL.

Uso:
  docker exec -it backend-prod python -m tools.import_csv \
      --arquivo /tmp/log.csv --device-id estufa01 [--dry-run]
"""
import argparse
import csv
from datetime import datetime, timezone, timedelta

AUTOMATICOS = ["vent", "aquec", "exaus", "led", "valv1", "valv2"]
MANUAIS = ["mvent", "maquec", "mexaus", "mled", "mvalv1", "mvalv2"]


def _int(v):
    v = (v or "").strip()
    return int(v) if v not in ("", "-") else None


def _float(v):
    v = (v or "").strip().replace(",", ".")
    return float(v) if v not in ("", "-") else None


def _ts(row):
    ep = (row.get("epoch") or "").strip()
    if ep:
        return datetime.fromtimestamp(int(ep), tz=timezone.utc)
    dh = (row.get("datahora") or "").strip()
    if dh:  # fallback: datahora é hora local de Brasília (UTC-3)
        return datetime.strptime(dh, "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone(timedelta(hours=-3)))
    return datetime.now(timezone.utc)


def _atuadores(row):
    d = {k: _int(row.get(k)) for k in AUTOMATICOS}
    d["manual"] = {k[1:]: _int(row.get(k)) for k in MANUAIS}  # mvent -> vent
    return d


def classificar(row):
    """Retorna ('telemetria'|'evento', kwargs) a partir de uma linha do CSV."""
    origem = (row.get("origem") or "").strip() or None
    if (row.get("tipo") or "").strip().upper() == "E":
        return "evento", {
            "ts": _ts(row),
            "tipo": (row.get("evento") or "").strip() or "evento",
            "detalhe": {
                "temp": _float(row.get("temp")),
                "umid1": _int(row.get("umid1")),
                "umid2": _int(row.get("umid2")),
                **{k: _int(row.get(k)) for k in AUTOMATICOS},
                "manual": {k[1:]: _int(row.get(k)) for k in MANUAIS},
            },
            "origem": origem,
        }
    return "telemetria", {
        "ts": _ts(row),
        "temp": _float(row.get("temp")),
        "u1": _int(row.get("umid1")),
        "u2": _int(row.get("umid2")),
        "uctrl1": _int(row.get("uctrl1")),
        "uctrl2": _int(row.get("uctrl2")),
        "ucorr1": _int(row.get("ucorr1")),
        "ucorr2": _int(row.get("ucorr2")),
        "atuadores": _atuadores(row),
        "origem": origem,
    }


def cutoff_do_banco(device_id):
    """Maior epoch ja gravado (telemetria ou evento) para o device. 0 se vazio."""
    from app.db import SessionLocal, Base, engine
    from app import models
    from sqlalchemy import select, func
    Base.metadata.create_all(bind=engine)
    with SessionLocal() as s:
        mt = s.execute(select(func.max(models.Telemetria.ts)).where(models.Telemetria.device_id == device_id)).scalar()
        me = s.execute(select(func.max(models.Evento.ts)).where(models.Evento.device_id == device_id)).scalar()
    vals = [v for v in (mt, me) if v is not None]
    return int(max(vals).timestamp()) if vals else 0


def carregar(arquivo, sep, cutoff):
    """Le o CSV, pula linhas com epoch <= cutoff, classifica o resto."""
    registros = []
    n_tel = n_ev = n_pulados = 0
    amostra = []
    with open(arquivo, newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f, delimiter=sep):
            ep = (row.get("epoch") or "").strip()
            if cutoff and ep.isdigit() and int(ep) <= cutoff:
                n_pulados += 1
                continue
            destino, kwargs = classificar(row)
            n_ev += destino == "evento"
            n_tel += destino == "telemetria"
            if len(amostra) < 3:
                amostra.append((destino, kwargs))
            registros.append((destino, kwargs))
    return registros, n_tel, n_ev, n_pulados, amostra


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arquivo", required=True)
    ap.add_argument("--device-id", required=True)
    ap.add_argument("--sep", default=",")
    ap.add_argument("--desde-epoch", type=int, default=None,
                    help="cutoff manual; por padrao usa o maior epoch ja no banco")
    ap.add_argument("--tudo", action="store_true",
                    help="ignora o cutoff e processa todas as linhas")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.tudo:
        cutoff = 0
    elif args.desde_epoch is not None:
        cutoff = args.desde_epoch
    else:
        cutoff = cutoff_do_banco(args.device_id)

    registros, n_tel, n_ev, n_pulados, amostra = carregar(args.arquivo, args.sep, cutoff)
    print(f"cutoff: pula epoch <= {cutoff}  |  ja no banco (pulados): {n_pulados}  |  "
          f"a importar: telemetria={n_tel} eventos={n_ev}")

    if args.dry_run:
        for destino, kw in amostra:
            print(f"[{destino}] {kw}")
        return
    if not registros:
        print("nada novo para importar.")
        return

    from sqlalchemy.dialects.postgresql import insert as pg_insert

    from app.db import SessionLocal, Base, engine
    from app import models
    from app.dedup import fp_evento, fp_telemetria
    Base.metadata.create_all(bind=engine)
    modelo = {"telemetria": models.Telemetria, "evento": models.Evento}

    inseridos = 0
    with SessionLocal() as s:
        for i, (destino, kwargs) in enumerate(registros, 1):
            epoch = int(kwargs["ts"].timestamp())
            if destino == "telemetria":
                fp = fp_telemetria(args.device_id, epoch, kwargs["temp"], kwargs["u1"],
                                   kwargs["u2"], kwargs["uctrl1"], kwargs["uctrl2"], kwargs["origem"])
            else:
                fp = fp_evento(args.device_id, epoch, kwargs["tipo"], kwargs["origem"])
            stmt = pg_insert(modelo[destino]).values(
                device_id=args.device_id, fp=fp, **kwargs
            ).on_conflict_do_nothing(index_elements=["fp"]).returning(modelo[destino].id)
            # RETURNING so devolve linha quando de fato insere; conflito -> vazio.
            if s.execute(stmt).first() is not None:
                inseridos += 1
            if i % 1000 == 0:
                s.commit()
        s.commit()
    # 'inseridos' = linhas realmente gravadas (o ON CONFLICT ignora as ja existentes)
    print(f"OK: {inseridos} linha(s) nova(s) gravada(s) por fingerprint "
          f"(cutoff pulou {n_pulados}) em device_id={args.device_id}")


if __name__ == "__main__":
    main()
