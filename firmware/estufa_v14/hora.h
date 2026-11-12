// Relogio: RTC DS3231, NTP e fotoperiodo (dia/noite).
#pragma once

#include <Arduino.h>

extern const char* TZ_ESTUFA;
extern bool horaValida, ntpGravadoNoRtc;
extern String fonteHora;

String horaFormatada();
String horaISO();
int  horaAtual();
bool ehDia();
void iniciarRtc();
void sincronizarNtp();
void verificarNtp();
