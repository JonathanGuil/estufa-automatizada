# Atalhos de operação. Rode a partir da raiz do repositório.
IP ?= IP_DA_VM
INFRA = cd infra && docker compose -f docker-compose.yml
PROD  = cd infra && docker compose --env-file env/.env.prod -f docker-compose.prod.yml
HML   = cd infra && docker compose --env-file env/.env.hml  -f docker-compose.hml.yml

.PHONY: help net build certs passwd infra-up infra-down prod-up prod-down prod-logs hml-up hml-down hml-logs backup ps

help:
	@echo "Alvos: net build certs passwd infra-up prod-up hml-up prod-logs hml-logs backup ps"
	@echo "Ordem 1a vez: net -> certs -> passwd -> build -> infra-up -> prod-up -> hml-up"

net:
	docker network inspect estufa_net >/dev/null 2>&1 || docker network create estufa_net

build:
	docker build -t estufa-backend:latest ./backend

certs:
	bash infra/mosquitto/gen-ca.sh $(IP)

# Gera o password_file do Mosquitto. Informe as senhas:
#   make passwd BACKEND_PW=xxxx ESP32_PW=yyyy
passwd:
	@test -n "$(BACKEND_PW)" || (echo "defina BACKEND_PW=..."; exit 1)
	@test -n "$(ESP32_PW)" || (echo "defina ESP32_PW=..."; exit 1)
	docker run --rm -v $(PWD)/infra/mosquitto:/m eclipse-mosquitto:2 sh -c \
	  "mosquitto_passwd -c -b /m/passwd backend '$(BACKEND_PW)' && mosquitto_passwd -b /m/passwd esp32 '$(ESP32_PW)'"
	@echo "passwd gerado em infra/mosquitto/passwd"

infra-up:
	$(INFRA) up -d
infra-down:
	$(INFRA) down

prod-up:
	$(PROD) up -d
prod-down:
	$(PROD) down
prod-logs:
	$(PROD) logs -f --tail=100

hml-up:
	$(HML) up -d
hml-down:
	$(HML) down
hml-logs:
	$(HML) logs -f --tail=100

backup:
	@mkdir -p backups
	docker exec postgres-prod pg_dump -U estufa estufa_prod | gzip > backups/prod_$$(date +%Y%m%d_%H%M).sql.gz
	@echo "backup salvo em backups/"

ps:
	docker ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
