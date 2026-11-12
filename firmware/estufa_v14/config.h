// Configuracao de compilacao: credenciais (secrets.h), broker MQTT e identificador do dispositivo.
// secrets.h fica ao lado deste arquivo e fora do Git; o modelo esta em secrets.example.h.
#pragma once

#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  else
#    error "Falta secrets.h: copie secrets.example.h para secrets.h e preencha Wi-Fi, OTA e MQTT."
#  endif
#else
#  include "secrets.h"
#endif
#if !defined(WIFI_SSID) || !defined(WIFI_SENHA) || !defined(OTA_SENHA) || !defined(MQTT_HOST) || !defined(MQTT_SENHA)
#  error "secrets.h incompleto: defina WIFI_SSID, WIFI_SENHA, OTA_SENHA, MQTT_HOST e MQTT_SENHA."
#endif
#ifndef OTA_HOSTNAME
#  define OTA_HOSTNAME "estufa-esp32"
#endif

#ifndef MQTT_PORT
#  define MQTT_PORT 8883
#endif
#ifndef MQTT_USUARIO
#  define MQTT_USUARIO "esp32"
#endif
#ifndef DEVICE_ID
#  define DEVICE_ID "estufa01"
#endif

#define FIRMWARE_VERSAO "14"
