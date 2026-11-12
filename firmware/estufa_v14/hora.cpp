// Relogio: RTC DS3231, NTP e fotoperiodo (dia/noite).

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "hora.h"
#include "parametros.h"

// Fuso horario e servidores NTP
const char* TZ_ESTUFA = "<-03>3";
const char* NTP_1 = "a.ntp.br";
const char* NTP_2 = "pool.ntp.org";

// RTC DS3231 (I2C)
const int PINO_SDA = 21;
const int PINO_SCL = 22;
RTC_DS3231 rtc;
bool  rtcPresente = false, horaValida = false, ntpGravadoNoRtc = false;
String fonteHora = "sem hora";
unsigned long ultimaChecagemNtp = 0;
const unsigned long INTERVALO_CHECAGEM_NTP = 10000;

// Data e hora local no formato dd/mm/aaaa hh:mm:ss.
String horaFormatada() {
  if (!horaValida) return "--/--/---- --:--:--";
  time_t a = time(nullptr); struct tm l; localtime_r(&a, &l);
  char b[24]; strftime(b, sizeof(b), "%d/%m/%Y %H:%M:%S", &l); return String(b);
}

// Data e hora local no formato aaaa-mm-dd hh:mm:ss (coluna datahora do CSV).
String horaISO() {
  time_t a = time(nullptr); struct tm l; localtime_r(&a, &l);
  char b[24]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &l); return String(b);
}

// Hora local (0-23), ou -1 se o relogio ainda nao tem hora valida.
int horaAtual() {
  if (!horaValida) return -1;
  time_t a = time(nullptr); struct tm l; localtime_r(&a, &l); return l.tm_hour;
}

// Indica se o horario atual esta dentro do fotoperiodo (sem hora valida, considera dia).
bool ehDia() {
  int h = horaAtual();
  if (h < 0) return true;
  if (ledHoraLiga < ledHoraDesliga)
    return (h >= ledHoraLiga && h < ledHoraDesliga);
  return (h >= ledHoraLiga || h < ledHoraDesliga);
}

// Inicializa o RTC DS3231 e, se ele tiver hora valida, acerta o relogio do sistema.
void iniciarRtc() {
  Wire.begin(PINO_SDA, PINO_SCL);
  rtcPresente = rtc.begin();
  if (!rtcPresente) { fonteHora = "RTC nao encontrado"; return; }
  if (rtc.lostPower()) { fonteHora = "RTC sem hora valida"; return; }
  DateTime a = rtc.now();
  struct timeval tv = { (time_t)a.unixtime(), 0 };
  settimeofday(&tv, nullptr);
  horaValida = true; fonteHora = "RTC";
}

// Configura o fuso e os servidores NTP.
void sincronizarNtp() { configTzTime(TZ_ESTUFA, NTP_1, NTP_2); }

// Apos a primeira sincronizacao NTP valida, marca a hora como valida e grava no RTC.
void verificarNtp() {
  if (ntpGravadoNoRtc || WiFi.status() != WL_CONNECTED) return;
  if (millis() - ultimaChecagemNtp < INTERVALO_CHECAGEM_NTP) return;
  ultimaChecagemNtp = millis();
  time_t a = time(nullptr);
  if (a < 1700000000) return;
  horaValida = true; fonteHora = "NTP";
  if (rtcPresente) rtc.adjust(DateTime((uint32_t)a));
  ntpGravadoNoRtc = true;
}
