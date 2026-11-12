// Conexao Wi-Fi.
#pragma once

#include <Arduino.h>

extern bool wifiConectado;
extern long quedasWifi;

void conectarWiFi();
void verificarWifi();
