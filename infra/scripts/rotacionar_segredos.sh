#!/usr/bin/env bash
# Troca senhas que hoje se repetem por senhas novas e distintas, geradas na propria VM
# (nada passa por chat, e-mail ou Git).
#
# Uso (na VM):   bash ~/estufa-nuvem/infra/scripts/rotacionar_segredos.sh
#
# Troca, nos dois ambientes:
#   - AUTH_PASS       login do painel (prod e hml recebem a mesma senha nova, mostrada no fim);
#                     se a senha tiver sido trocada pelo painel, essa troca e desfeita
#   - SESSION_SECRET  um diferente por ambiente (quem estiver logado precisa entrar de novo)
#   - MQTT_PASS       senha do usuario "backend" no Mosquitto
# Nao troca:
#   - a senha do usuario "esp32" no MQTT (exigiria regravar o ESP32)
#   - a senha do Postgres (o banco so e acessivel dentro da rede Docker)
#   - a senha do SSH: troque com o comando  passwd
# Se algo falhar no caminho, volta tudo como estava.
set -euo pipefail

RAIZ="${ESTUFA_DIR:-$HOME/estufa-nuvem}"
TS="$(date +%Y%m%d_%H%M%S)"
cd "$RAIZ"

log()  { printf '\n\033[1;32m==> %s\033[0m\n' "$*"; }
erro() { printf '\033[1;31m[erro] %s\033[0m\n' "$*" >&2; }
rodando() { docker ps --format '{{.Names}}' | grep -x "$1" >/dev/null; }
gerar() {   # $1 = tamanho; so letras e numeros (seguro em URL, sed e .env)
  local s; s="$(openssl rand -base64 96 | tr -dc 'A-Za-z0-9' | cut -c1-"$1")"
  [ "${#s}" -eq "$1" ] || { erro "falha ao gerar senha"; return 1; }
  printf '%s' "$s"
}
esperar() {  # $1 = container: saudavel e conectado ao MQTT
  local c="$1" st="?" saida
  for _ in $(seq 1 50); do
    st="$(docker inspect -f '{{.State.Health.Status}}' "$c" 2>/dev/null || echo ausente)"
    [ "$st" = healthy ] && break
    sleep 3
  done
  [ "$st" = healthy ] || { erro "$c nao ficou saudavel"; docker logs --tail 30 "$c" 2>&1 || true; return 1; }
  for _ in $(seq 1 20); do
    saida="$(docker logs "$c" 2>&1 || true)"
    grep -q "MQTT conectado" <<<"$saida" && return 0
    sleep 2
  done
  erro "$c nao conectou ao MQTT com a senha nova"; docker logs --tail 30 "$c" 2>&1 || true
  return 1
}
recriar() {  # $1 = prod|hml
  ( cd "$RAIZ/infra" && docker compose --env-file "env/.env.$1" -f "docker-compose.$1.yml" \
      up -d --no-deps --force-recreate "backend-$1" )
}
gravar_passwd() {  # $1 = arquivo de origem (relativo a infra/mosquitto); escreve no lugar (mantem o bind mount)
  docker run --rm -v "$RAIZ/infra/mosquitto:/m" eclipse-mosquitto:2 \
    sh -c 'cat "/m/$0" > /m/passwd && chmod 644 /m/passwd' "$1"
  docker kill -s HUP mosquitto >/dev/null     # Mosquitto relê o passwd sem derrubar ninguem
}
definir() {  # $1 = prod|hml, $2 = chave, $3 = valor
  local f="infra/env/.env.$1"
  if grep -q "^$2=" "$f"; then sed -i "s|^$2=.*|$2=$3|" "$f"; else printf '%s=%s\n' "$2" "$3" >> "$f"; fi
  grep -qx "$2=$3" "$f"
}

desfazer() {
  set +e; trap - ERR
  erro "Falhou. Voltando as senhas anteriores..."
  cp -p "infra/env/.env.prod.bak-$TS" infra/env/.env.prod
  cp -p "infra/env/.env.hml.bak-$TS"  infra/env/.env.hml
  gravar_passwd "passwd.bak-$TS"
  rodando postgres-hml && recriar hml
  recriar prod && esperar backend-prod && erro "Senhas anteriores restauradas; producao de volta ao normal."
  exit 1
}

# ---------- verificacoes ----------
for f in infra/env/.env.prod infra/env/.env.hml infra/mosquitto/passwd; do
  [ -f "$f" ] || { erro "arquivo ausente: $RAIZ/$f"; exit 1; }
done
for c in mosquitto backend-prod postgres-prod; do
  rodando "$c" || { erro "$c nao esta rodando"; exit 1; }
done
command -v openssl >/dev/null || { erro "openssl nao encontrado"; exit 1; }

log "Guardando copias das senhas atuais (*.bak-$TS)"
cp -p infra/env/.env.prod "infra/env/.env.prod.bak-$TS"
cp -p infra/env/.env.hml  "infra/env/.env.hml.bak-$TS"
cp infra/mosquitto/passwd "infra/mosquitto/passwd.bak-$TS"
chmod 600 "infra/env/.env.prod.bak-$TS" "infra/env/.env.hml.bak-$TS"

AUTH="$(gerar 20)"; MQTT="$(gerar 32)"; SEC_PROD="$(gerar 48)"; SEC_HML="$(gerar 48)"

trap desfazer ERR

log "Trocando a senha do usuario backend no Mosquitto"
docker run --rm -v "$RAIZ/infra/mosquitto:/m" eclipse-mosquitto:2 \
  sh -c 'cp /m/passwd /tmp/p && mosquitto_passwd -b /tmp/p backend "$0" && cat /tmp/p > /m/passwd && chmod 644 /m/passwd' "$MQTT"
docker kill -s HUP mosquitto >/dev/null

log "Atualizando infra/env/.env.prod e .env.hml"
definir prod AUTH_PASS "$AUTH"; definir prod SESSION_SECRET "$SEC_PROD"; definir prod MQTT_PASS "$MQTT"
definir hml  AUTH_PASS "$AUTH"; definir hml  SESSION_SECRET "$SEC_HML";  definir hml  MQTT_PASS "$MQTT"

if rodando postgres-hml; then
  log "Recriando backend-hml"; recriar hml; esperar backend-hml
fi
log "Recriando backend-prod"; recriar prod; esperar backend-prod

# A senha trocada pelo painel fica no banco e tem prioridade sobre o AUTH_PASS: apaga-a
# para valer a senha nova gerada aqui (e encerra as sessoes abertas).
for amb in prod hml; do
  if rodando "backend-$amb" && docker exec "backend-$amb" test -f /app/app/senha.py; then
    docker exec "backend-$amb" python -m app.senha --usar-env >/dev/null
  fi
done

trap - ERR
log "Senhas trocadas. Producao saudavel e conectada ao MQTT."
echo
echo "   Nova senha do painel (usuario $(grep '^AUTH_USER=' infra/env/.env.prod | cut -d= -f2)):"
echo
echo "       $AUTH"
echo
echo "   Guarde num gerenciador de senhas. Ela fica so em infra/env/.env.prod e .env.hml."
echo "   Copias antigas: infra/env/*.bak-$TS e infra/mosquitto/passwd.bak-$TS (apague quando quiser)."
echo "   Falta trocar a senha do SSH:  passwd"
