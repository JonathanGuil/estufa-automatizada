#!/usr/bin/env bash
# Volta o projeto da VM para um backup feito pelo publicar.sh.
#
# Uso (na VM):   bash reverter.sh ~/estufa-nuvem_backup_AAAAMMDD_HHMMSS
#
# Copia os arquivos do backup por cima da pasta atual, reativa a imagem do backend
# daquele momento (ou reconstroi a partir dos arquivos restaurados) e recria os
# backends. O banco NAO e alterado; para restaurar o banco use o .sql.gz em backups/.
set -euo pipefail

BACKUP="${1:?uso: bash reverter.sh <pasta de backup>}"
RAIZ="${ESTUFA_DIR:-$HOME/estufa-nuvem}"
IMG="estufa-backend"
BACKUP="${BACKUP%/}"
TS="${BACKUP##*_backup_}"

[ -d "$BACKUP/backend" ] || { echo "backup invalido: $BACKUP" >&2; exit 1; }
rodando() { docker ps --format '{{.Names}}' | grep -x "$1" >/dev/null; }

echo "==> Restaurando arquivos de $BACKUP"
# Segredos (.env, passwd, certs) nunca sao trocados pelo publicar.sh: ficam como estao.
# cp sobrescreve no lugar, mantendo os bind mounts dos containers.
( cd "$BACKUP" && find . -type f \
    ! -path './infra/env/.env.prod' ! -path './infra/env/.env.hml' \
    ! -path './infra/mosquitto/passwd' ! -path './infra/mosquitto/certs/*' -print0 ) |
  while IFS= read -r -d '' f; do
    mkdir -p "$RAIZ/$(dirname "$f")"
    cp -p "$BACKUP/$f" "$RAIZ/$f" || echo "aviso: nao restaurou $f" >&2
  done

if docker image inspect "$IMG:pre_$TS" >/dev/null 2>&1; then
  echo "==> Reativando a imagem $IMG:pre_$TS"
  docker tag "$IMG:pre_$TS" "$IMG:latest"
else
  echo "==> Reconstruindo a imagem a partir dos arquivos restaurados"
  docker build -q -t "$IMG:latest" "$RAIZ/backend"
fi

for amb in hml prod; do
  if [ "$amb" = prod ] || rodando "postgres-hml"; then
    echo "==> Recriando backend-$amb"
    ( cd "$RAIZ/infra" && docker compose --env-file "env/.env.$amb" -f "docker-compose.$amb.yml" \
        up -d --no-deps --force-recreate "backend-$amb" )
  fi
done

for _ in $(seq 1 50); do
  st="$(docker inspect -f '{{.State.Health.Status}}' backend-prod 2>/dev/null || echo ?)"
  [ "$st" = healthy ] && { echo "==> backend-prod saudavel. Reversao concluida."; exit 0; }
  sleep 3
done
echo "backend-prod nao ficou saudavel; veja: docker logs backend-prod" >&2
exit 1
