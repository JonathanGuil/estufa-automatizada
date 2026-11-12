#!/usr/bin/env bash
# Publica uma versao nova do projeto na VM, com backup e volta automatica se falhar.
#
# Uso (na VM):   bash publicar.sh ~/estufa-nuvem_v2.tgz
#
# O pacote traz so codigo. Os segredos da VM (infra/env/.env.prod e .env.hml,
# infra/mosquitto/passwd e certs/) nao vem nele e nao sao tocados.
#
# Etapas:
#   1. backup da pasta (~/estufa-nuvem_backup_<data>) e do banco de producao
#   2. extrai o pacote por cima da pasta atual
#   3. reconstroi a imagem do backend (a anterior fica marcada como :pre_<data>)
#   4. testa no hml, se ele estiver rodando
#   5. troca o backend de producao
#   6. confere saude (healthcheck) e conexao MQTT
# Se 4, 5 ou 6 falharem, a versao anterior e restaurada sozinha.
set -euo pipefail

PACOTE="${1:?uso: bash publicar.sh <pacote.tgz>}"
RAIZ="${ESTUFA_DIR:-$HOME/estufa-nuvem}"
TS="$(date +%Y%m%d_%H%M%S)"
BACKUP="${RAIZ}_backup_${TS}"
IMG="estufa-backend"

log()   { printf '\n\033[1;32m==> %s\033[0m\n' "$*"; }
aviso() { printf '\033[1;33m[aviso] %s\033[0m\n' "$*"; }
erro()  { printf '\033[1;31m[erro] %s\033[0m\n' "$*" >&2; }

# (sem "grep -q" em pipes: com pipefail, o SIGPIPE do produtor viraria falha falsa)
rodando() { docker ps --format '{{.Names}}' | grep -x "$1" >/dev/null; }

esperar_saude() {   # $1 = container; espera "healthy" por ate ~150 s
  local c="$1" i st="?"
  for i in $(seq 1 50); do
    st="$(docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "$c" 2>/dev/null || echo ausente)"
    [ "$st" = healthy ] && return 0
    case "$st" in unhealthy|exited|dead|ausente) [ "$i" -gt 5 ] && break ;; esac
    sleep 3
  done
  erro "$c nao ficou saudavel (estado: $st). Fim do log:"
  docker logs --tail 40 "$c" 2>&1 || true
  return 1
}

esperar_mqtt() {    # $1 = container recem-criado; o log precisa mostrar a conexao ao broker
  local c="$1" i
  local saida
  for i in $(seq 1 20); do
    saida="$(docker logs "$c" 2>&1 || true)"
    grep -q "MQTT conectado" <<<"$saida" && return 0
    sleep 2
  done
  erro "$c nao conectou ao MQTT. Fim do log:"
  docker logs --tail 40 "$c" 2>&1 || true
  return 1
}

TROCADOS=""         # ambientes cujo backend ja foi recriado com a versao nova
recriar() {         # $1 = prod|hml
  TROCADOS="$TROCADOS $1"
  ( cd "$RAIZ/infra" && docker compose --env-file "env/.env.$1" -f "docker-compose.$1.yml" \
      up -d --no-deps --force-recreate "backend-$1" )
}

restaurar() {
  set +e                                           # restauracao segue ate o fim mesmo com falhas pontuais
  trap - ERR
  erro "Algo falhou. Restaurando a versao anterior a partir de $BACKUP ..."
  # Volta exatamente os arquivos que o pacote trouxe: os que existiam saem do backup
  # (cp sobrescreve no lugar, mantendo os bind mounts); os que nao existiam sao removidos.
  local m rel
  while IFS= read -r m; do
    rel="${m#estufa-nuvem/}"
    case "$m" in */|"") continue ;; esac
    if [ -f "$BACKUP/$rel" ]; then cp -p "$BACKUP/$rel" "$RAIZ/$rel" || erro "nao restaurou $rel"
    else rm -f "$RAIZ/$rel"; fi
  done <<<"$LISTA"
  if docker image inspect "$IMG:pre_$TS" >/dev/null 2>&1; then
    docker tag "$IMG:pre_$TS" "$IMG:latest"
  else
    docker build -t "$IMG:latest" "$RAIZ/backend"
  fi
  local amb
  for amb in $TROCADOS; do                         # so mexe no que ja tinha sido trocado
    ( cd "$RAIZ/infra" && docker compose --env-file "env/.env.$amb" -f "docker-compose.$amb.yml" \
        up -d --no-deps --force-recreate "backend-$amb" ) && esperar_saude "backend-$amb" \
      || erro "backend-$amb nao voltou; veja: docker logs backend-$amb"
  done
  erro "Versao anterior restaurada. Nada do pacote novo ficou em uso no backend."
  exit 1
}

# ---------- verificacoes antes de mexer em qualquer coisa ----------
[ -f "$PACOTE" ] || { erro "pacote nao encontrado: $PACOTE"; exit 1; }
[ -d "$RAIZ" ]   || { erro "pasta do projeto nao encontrada: $RAIZ"; exit 1; }
LISTA="$(tar -tzf "$PACOTE")"
grep -qx 'estufa-nuvem/backend/app/main.py' <<<"$LISTA" || { erro "pacote invalido (esperado estufa-nuvem/...)"; exit 1; }
if grep -E '(^|/)\.env\.(prod|hml)$|mosquitto/passwd$|mosquitto/certs/.|secrets\.h$' <<<"$LISTA"; then
  erro "o pacote contem arquivos de segredo (listados acima); abortando"; exit 1
fi
for f in infra/env/.env.prod infra/env/.env.hml infra/mosquitto/passwd infra/mosquitto/certs/server.key; do
  [ -f "$RAIZ/$f" ] || { erro "segredo ausente na VM: $RAIZ/$f"; exit 1; }
done
rodando postgres-prod || { erro "postgres-prod nao esta rodando; suba o ambiente antes (make prod-up)"; exit 1; }
rodando mosquitto     || { erro "mosquitto nao esta rodando; suba a infra antes (make infra-up)"; exit 1; }

# ---------- 1. backups ----------
log "1/6 Backup da pasta em $BACKUP"
mkdir -p "$BACKUP"
tar -C "$RAIZ" --exclude=./backups -cf - . | tar -C "$BACKUP" -xpf -
log "1/6 Backup do banco de producao"
mkdir -p "$RAIZ/backups"
DUMP="$RAIZ/backups/prod_antes_publicacao_${TS}.sql.gz"
docker exec postgres-prod pg_dump -U estufa estufa_prod | gzip > "$DUMP"
FIM_DUMP="$(zcat "$DUMP" | tail -n 5)"       # pg_dump termina com "dump complete" quando deu certo
grep -q "dump complete" <<<"$FIM_DUMP" || { erro "dump do banco incompleto: $DUMP"; exit 1; }
echo "   $DUMP ($(du -h "$DUMP" | cut -f1))"
if rodando backend-prod; then
  docker tag "$(docker inspect -f '{{.Image}}' backend-prod)" "$IMG:pre_$TS"
  echo "   imagem atual do backend marcada como $IMG:pre_$TS"
fi

trap restaurar ERR

# ---------- 2. codigo novo ----------
log "2/6 Extraindo o pacote"
tar -xzf "$PACOTE" -C "$RAIZ" --strip-components=1 --overwrite --no-same-owner
for f in infra/env/.env.prod infra/env/.env.hml infra/mosquitto/passwd infra/mosquitto/certs/server.key; do
  [ -f "$RAIZ/$f" ] || { erro "segredo sumiu apos extrair: $f"; false; }
done

# ---------- 3. imagem ----------
log "3/6 Reconstruindo a imagem do backend"
docker build -q -t "$IMG:latest" "$RAIZ/backend"

# ---------- 4. hml ----------
if rodando postgres-hml; then
  log "4/6 Testando no hml"
  recriar hml; esperar_saude backend-hml; esperar_mqtt backend-hml
else
  aviso "4/6 hml desligado; pulando o teste em hml"
fi

# ---------- 5/6. prod ----------
log "5/6 Trocando o backend de producao"
recriar prod
log "6/6 Conferindo producao"
esperar_saude backend-prod; esperar_mqtt backend-prod
if rodando caddy; then
  docker exec caddy grep -q 'data-view="clima"' /srv/frontend/index.html \
    && echo "   painel novo publicado pelo Caddy" \
    || aviso "o Caddy nao esta enxergando o index.html novo (confira o volume ../frontend)"
fi

trap - ERR
[ -f "$HOME/estufa-nuvem-src.tgz" ] && chmod 600 "$HOME/estufa-nuvem-src.tgz" || true

log "Publicado. Producao saudavel e conectada ao MQTT."
echo "   Backup da pasta: $BACKUP"
echo "   Backup do banco: $DUMP"
echo "   Para voltar:     bash $RAIZ/infra/scripts/reverter.sh $BACKUP"
