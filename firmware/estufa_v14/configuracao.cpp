// Parametros em JSON: leitura, aplicacao, persistencia em /config.json.

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include "configuracao.h"
#include "parametros.h"
#include "armazenamento.h"

// Arquivo de parametros
const char* ARQ_CONFIG = "/config.json";
const int   CONFIG_VERSAO = 2;

// Bloco {...} associado a uma chave do JSON, ou "" se ausente.
String cfgBloco(const String& js, const String& chave) {
  int k = js.indexOf("\"" + chave + "\"");
  if (k < 0) return "";
  int ab = js.indexOf('{', k);
  if (ab < 0) return "";
  int nivel = 0;
  for (int i = ab; i < (int)js.length(); i++) {
    char c = js.charAt(i);
    if (c == '{') nivel++;
    else if (c == '}') { if (--nivel == 0) return js.substring(ab, i + 1); }
  }
  return "";
}

// Le o numero de uma chave do JSON.
bool cfgNum(const String& js, const String& chave, float& out) {
  int k = js.indexOf("\"" + chave + "\"");
  if (k < 0) return false;
  int c = js.indexOf(':', k);
  if (c < 0) return false;
  int i = c + 1;
  while (i < (int)js.length() && (js.charAt(i) == ' ' || js.charAt(i) == '\t')) i++;
  if (i >= (int)js.length()) return false;
  char c0 = js.charAt(i);
  if (!((c0 >= '0' && c0 <= '9') || c0 == '-' || c0 == '+' || c0 == '.')) return false;
  int j = i;
  while (j < (int)js.length()) {
    char ch = js.charAt(j);
    if (ch == ',' || ch == '}' || ch == ']' || ch == ' ') break;
    j++;
  }
  if (j <= i) return false;
  out = js.substring(i, j).toFloat();
  return true;
}

// Le o par [a,b] de uma chave do JSON.
bool cfgPar(const String& js, const String& chave, float& a, float& b) {
  int k = js.indexOf("\"" + chave + "\"");
  if (k < 0) return false;
  int ab = js.indexOf('[', k);
  if (ab < 0) return false;
  int fim = js.indexOf(']', ab);
  if (fim < 0) return false;
  String par = js.substring(ab + 1, fim);
  int v = par.indexOf(',');
  if (v < 0) return false;
  a = par.substring(0, v).toFloat();
  b = par.substring(v + 1).toFloat();
  return true;
}

// Le o booleano de uma chave do JSON.
bool cfgBool(const String& js, const String& chave, bool& out) {
  int k = js.indexOf("\"" + chave + "\"");
  if (k < 0) return false;
  int c = js.indexOf(':', k);
  if (c < 0) return false;
  String r = js.substring(c + 1);
  r.trim();
  if (r.startsWith("true"))  { out = true;  return true; }
  if (r.startsWith("false")) { out = false; return true; }
  return false;
}

// Le o texto de uma chave do JSON.
bool cfgStr(const String& js, const String& chave, String& out) {
  int k = js.indexOf("\"" + chave + "\"");
  if (k < 0) return false;
  int dp = js.indexOf(':', k);        if (dp < 0) return false;
  int q1 = js.indexOf('"', dp + 1);   if (q1 < 0) return false;
  int q2 = js.indexOf('"', q1 + 1);   if (q2 < 0) return false;
  out = js.substring(q1 + 1, q2);
  return true;
}

// Aplica um JSON de parametros; campos ausentes ficam como estao e cada valor e limitado a uma faixa segura.
void aplicarConfigJson(const String& js) {
  float v, a, b; bool flag;

  { String bl = cfgBloco(js, "fotoperiodo");
    if (bl.length()) {
      if (cfgNum(bl, "liga_h", v))     ledHoraLiga    = (int)constrain(v, 0, 23);
      if (cfgNum(bl, "desliga_h", v))  ledHoraDesliga = (int)constrain(v, 0, 23);
    } }

  { String bl = cfgBloco(js, "aquecedor");
    if (bl.length()) {
      if (cfgNum(bl, "liga_dia_c", v))      aquecLigaDia      = constrain(v, 0.0, 50.0);
      if (cfgNum(bl, "desliga_dia_c", v))   aquecDesligaDia   = constrain(v, 0.0, 50.0);
      if (cfgNum(bl, "liga_noite_c", v))    aquecLigaNoite    = constrain(v, 0.0, 50.0);
      if (cfgNum(bl, "desliga_noite_c", v)) aquecDesligaNoite = constrain(v, 0.0, 50.0);
    } }

  { String bl = cfgBloco(js, "ventilador");
    if (bl.length()) {
      if (cfgNum(bl, "liga_dia_c", v))      ventLigaDia      = constrain(v, 0.0, 50.0);
      if (cfgNum(bl, "desliga_dia_c", v))   ventDesligaDia   = constrain(v, 0.0, 50.0);
      if (cfgNum(bl, "liga_noite_c", v))    ventLigaNoite    = constrain(v, 0.0, 50.0);
      if (cfgNum(bl, "desliga_noite_c", v)) ventDesligaNoite = constrain(v, 0.0, 50.0);
    } }

  { String bl = cfgBloco(js, "exaustor");
    if (bl.length()) {
      if (cfgNum(bl, "liga_dia_c", v))      exausLigaDia      = constrain(v, 0.0, 50.0);
      if (cfgNum(bl, "desliga_dia_c", v))   exausDesligaDia   = constrain(v, 0.0, 50.0);
      if (cfgNum(bl, "liga_noite_c", v))    exausLigaNoite    = constrain(v, 0.0, 50.0);
      if (cfgNum(bl, "desliga_noite_c", v)) exausDesligaNoite = constrain(v, 0.0, 50.0);
    } }

  { String bl = cfgBloco(js, "circulacao");
    if (bl.length()) {
      if (cfgBool(bl, "habilitado", flag))      circHabilitado     = flag;
      if (cfgNum(bl, "hora_ini_h", v))          circHoraIni        = (int)constrain(v, 0, 23);
      if (cfgNum(bl, "hora_fim_h", v))          circHoraFim        = (int)constrain(v, 0, 24);
      if (cfgNum(bl, "liga_min", v))            circLigaMs         = (unsigned long)(constrain(v, 0.1, 60.0) * 60000.0);
      if (cfgNum(bl, "desliga_min", v))         circDeslMs         = (unsigned long)(constrain(v, 0.1, 240.0) * 60000.0);
      if (cfgBool(bl, "aciona_exaustor", flag)) circAcionaExaustor = flag;
    } }

  { String bl = cfgBloco(js, "irrigacao");
    if (bl.length()) {
      if (cfgPar(bl, "dose_ml", a, b)) {
        doseVolume[0] = (int)constrain(a, 1, 5000);
        doseVolume[1] = (int)constrain(b, 1, 5000);
      }
      if (cfgPar(bl, "intervalo_h", a, b)) {
        intervaloAgendado[0] = (unsigned long)constrain(a, 1, 720) * 3600000UL;
        intervaloAgendado[1] = (unsigned long)constrain(b, 1, 720) * 3600000UL;
      }
      if (cfgPar(bl, "vazao_ml_min", a, b)) {
        vazao[0] = (int)constrain(a, 1, 1000);
        vazao[1] = (int)constrain(b, 1, 1000);
      }
      if (cfgPar(bl, "limiar_saturado_adc", a, b)) {
        limiarSaturado[0] = (int)constrain(a, 0, 4095);
        limiarSaturado[1] = (int)constrain(b, 0, 4095);
      }
      if (cfgBool(bl, "supervisao_umidade", flag))             supervisaoUmidade = flag;
      if (cfgBool(bl, "dose_manual_reinicia_intervalo", flag)) doseManualReiniciaIntervalo = flag;
    } }
}

// Parametros efetivos em JSON (usado para persistir e para o eco em config/state).
String construirConfigJson() {
  String j = "{";
  j += "\"versao\":" + String(CONFIG_VERSAO) + ",";
  j += "\"fotoperiodo\":{\"liga_h\":" + String(ledHoraLiga) + ",\"desliga_h\":" + String(ledHoraDesliga) + "},";
  j += "\"aquecedor\":{\"liga_dia_c\":" + String(aquecLigaDia, 1) + ",\"desliga_dia_c\":" + String(aquecDesligaDia, 1)
     + ",\"liga_noite_c\":" + String(aquecLigaNoite, 1) + ",\"desliga_noite_c\":" + String(aquecDesligaNoite, 1) + "},";
  j += "\"ventilador\":{\"liga_dia_c\":" + String(ventLigaDia, 1) + ",\"desliga_dia_c\":" + String(ventDesligaDia, 1)
     + ",\"liga_noite_c\":" + String(ventLigaNoite, 1) + ",\"desliga_noite_c\":" + String(ventDesligaNoite, 1) + "},";
  j += "\"exaustor\":{\"liga_dia_c\":" + String(exausLigaDia, 1) + ",\"desliga_dia_c\":" + String(exausDesligaDia, 1)
     + ",\"liga_noite_c\":" + String(exausLigaNoite, 1) + ",\"desliga_noite_c\":" + String(exausDesligaNoite, 1) + "},";
  j += "\"circulacao\":{\"habilitado\":" + String(circHabilitado ? "true" : "false")
     + ",\"hora_ini_h\":" + String(circHoraIni) + ",\"hora_fim_h\":" + String(circHoraFim)
     + ",\"liga_min\":" + String(circLigaMs / 60000.0, 1) + ",\"desliga_min\":" + String(circDeslMs / 60000.0, 1)
     + ",\"aciona_exaustor\":" + String(circAcionaExaustor ? "true" : "false") + "},";
  j += "\"irrigacao\":{";
  j += "\"dose_ml\":[" + String(doseVolume[0]) + "," + String(doseVolume[1]) + "],";
  j += "\"intervalo_h\":[" + String(intervaloAgendado[0] / 3600000UL) + "," + String(intervaloAgendado[1] / 3600000UL) + "],";
  j += "\"vazao_ml_min\":[" + String(vazao[0]) + "," + String(vazao[1]) + "],";
  j += "\"limiar_saturado_adc\":[" + String(limiarSaturado[0]) + "," + String(limiarSaturado[1]) + "],";
  j += "\"supervisao_umidade\":" + String(supervisaoUmidade ? "true" : "false") + ",";
  j += "\"dose_manual_reinicia_intervalo\":" + String(doseManualReiniciaIntervalo ? "true" : "false");
  j += "}}";
  return j;
}

// Grava os parametros em /config.json.
void salvarConfig() {
  if (!fsPronto) return;
  File f = LittleFS.open(ARQ_CONFIG, "w");
  if (f) { f.print(construirConfigJson()); f.close(); }
}

// Carrega os parametros de /config.json, se existir.
void carregarConfig() {
  if (!fsPronto || !LittleFS.exists(ARQ_CONFIG)) return;
  File f = LittleFS.open(ARQ_CONFIG, "r");
  if (!f) return;
  String js = f.readString();
  f.close();
  if (js.length()) { aplicarConfigJson(js); Serial.println("Config carregada do LittleFS"); }
}
