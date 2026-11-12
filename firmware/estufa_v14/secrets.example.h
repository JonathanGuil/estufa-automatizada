// Copie para secrets.h (mesma pasta do .ino) e preencha com os valores reais.
// O secrets.h esta no .gitignore e nunca deve ser versionado.

#pragma once

// Wi-Fi da estufa
#define WIFI_SSID     "nome-da-rede"
#define WIFI_SENHA    "senha-do-wifi"

// Atualizacao pela rede (Arduino OTA)
#define OTA_HOSTNAME  "estufa-esp32"
#define OTA_SENHA     "senha-do-ota"

// Broker MQTT na VM (porta 8883, TLS com a CA propria pinada no .ino)
#define MQTT_HOST     "IP-publico-da-VM"
#define MQTT_USUARIO  "esp32"
#define MQTT_SENHA    "senha-do-usuario-esp32-no-mosquitto"