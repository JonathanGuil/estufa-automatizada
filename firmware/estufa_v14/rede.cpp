// Conexao Wi-Fi.

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "rede.h"
#include "hora.h"
#include "armazenamento.h"

bool wifiConectado = false;
unsigned long ultimaChecagemWifi = 0, desdeQuedaWifi = 0;
const unsigned long INTERVALO_CHECAGEM_WIFI = 5000;
const unsigned long TIMEOUT_RECONEXAO = 60000;
long quedasWifi = 0;

// Conecta ao Wi-Fi no boot (espera ate 15 s).
void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  unsigned long ini = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - ini < 15000) delay(300);
  wifiConectado = (WiFi.status() == WL_CONNECTED);
}

// Acompanha o Wi-Fi: registra quedas e retornos e forca reconexao apos o tempo limite.
void verificarWifi() {
  if (millis() - ultimaChecagemWifi < INTERVALO_CHECAGEM_WIFI) return;
  ultimaChecagemWifi = millis();
  bool ag = (WiFi.status() == WL_CONNECTED);

  if (!ag && wifiConectado) {
    wifiConectado = false; desdeQuedaWifi = millis(); quedasWifi++;
    registrarLinha('E', "wifi_off", "sistema"); return;
  }
  if (ag && !wifiConectado) {
    wifiConectado = true;
    registrarLinha('E', "wifi_on", "sistema");
    if (!ntpGravadoNoRtc) sincronizarNtp();
    return;
  }
  if (!ag && millis() - desdeQuedaWifi > TIMEOUT_RECONEXAO) {
    WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_SENHA);
    desdeQuedaWifi = millis();
  }
}
