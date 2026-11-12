#!/usr/bin/env bash
# Gera CA própria + certificado do servidor para o MQTT (8883).
# O ca.crt gerado vai pinado na firmware do ESP32.
set -euo pipefail
IP="${1:-IP_DA_VM}"
DIR="$(cd "$(dirname "$0")" && pwd)/certs"
DAYS=3650
mkdir -p "$DIR"; cd "$DIR"

openssl genrsa -out ca.key 4096
openssl req -x509 -new -nodes -key ca.key -sha256 -days "$DAYS" -subj "/CN=Estufa-CA" -out ca.crt

openssl genrsa -out server.key 2048
openssl req -new -key server.key -subj "/CN=$IP" -out server.csr

cat > san.ext <<EXT
subjectAltName = IP:$IP, DNS:mqtt.$IP.sslip.io
extendedKeyUsage = serverAuth
EXT

openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -days "$DAYS" -sha256 -extfile san.ext -out server.crt
rm -f server.csr san.ext ca.srl 2>/dev/null || true
chmod 644 *.crt; chmod 600 *.key

echo "OK -> certificados em $DIR"
echo "   Copie ca.crt para a firmware (trust store do ESP32)."
