// Comunicacao com a VM por MQTT/TLS: publicacao, reenvio do backlog, config/set e comando/set.

#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <math.h>
#include "config.h"
#include "mqtt.h"
#include "armazenamento.h"
#include "configuracao.h"
#include "sensores.h"
#include "janela.h"
#include "atuadores.h"
#include "controle.h"

// Certificado (publico) da CA propria que assinou o certificado do broker.
const char* CA_CERT = R"CACERT(
-----BEGIN CERTIFICATE-----
MIIFCTCCAvGgAwIBAgIUCbPYRUUkebDr0hDry1eyPJZAkV4wDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJRXN0dWZhLUNBMB4XDTI2MDgzMDE3NDMwMloXDTM2MDgy
NzE3NDMwMlowFDESMBAGA1UEAwwJRXN0dWZhLUNBMIICIjANBgkqhkiG9w0BAQEF
AAOCAg8AMIICCgKCAgEAxVSewavjTnMUKeM5C6Dz9dPqhsdFcGQ3i1rAaDHElMf/
CIQ577qGJQLasdMf+oJgqMGihZad8wcm6HBnEusFVBk/TLgaXxAMwio8DctOdx83
PS1qkf5oTLid04b19p1cJkGKKD/XjVlvwtLTZWLcwHMLyZBqErxZT+66sKWAKniK
vrW6Ct6StTtGMNo82Q9ybK33SSe4Lr9iuo+t1c3HcRBgcTbOptnnfwZnDGisw0QW
mNhB8TBWF/ym1sUE6bgEJc+oRotZp72GuFbZYlXCu/zi5pqrSZgaOXyELtPHpBo5
EFMQYuN0lDSAQciT5H3ySQkqpJHzvwK71ptB+3ZtQNwRsjvwnNc5xNRNjMCtR2sl
4kq8t4nTSJLtG57F4Qq/6MjhSstzwqBNDS4Tl2+HXQct4mnsKUWWjbazKSRkuWJy
nkc8B3ohJJaIsvEPcwaVGgTbf9KPPpIGH+3LdAB83SfwlkJ5S02icqMv9Ff9Q7dm
XXeQczvkvblLN9OBCfLIt2DXNs52HeGl85cjEKfHFU349D/LGs9YtW/uhlQPCctj
6RVZJxBHv6yOExLvegIIP1hCgbCtzOa0iY+tAsLz36YAkqs6wrTsXBTVatwM2wgi
BjQRRf5z9Ciss5l6inw2yVXx2AbrA+tx+lvkZB1jzWrc8fS+Gs9J2XWVqRqYm0EC
AwEAAaNTMFEwHQYDVR0OBBYEFEEz4+izf7aacLT2BHI7+hsgqjELMB8GA1UdIwQY
MBaAFEEz4+izf7aacLT2BHI7+hsgqjELMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZI
hvcNAQELBQADggIBAAIwSDiZoKt1pEewmkYoS9rHG0CceiqyfOGpHwPkp9L1U7QX
raCuY0rFF0R1XJVecoO+aieVS/a7LrQD6lqYS35gnYQHPSoO6oOLN0m7lCiNaXq+
ZrmA4h9AyHy8q/kv1HzADv/GwRMaS43Doy4nf2T5wb4Y6a1puZ6lae3cPbRe0D/Y
VjvNPjfw4J3wDixNtwyH+/eAHE7pOAw/g0fA2r+cV0fOIrH4yzJ1XHmOfEfID+b0
jgWsnxkBv3RoNVOkqGoZx+BcVsOsKn7Xye58EFJ7yv7OGWO3eiLOYgU42Rh75DGX
FHF3wJcWrRkIC+txjTbWHDO4SLrzb1PKv3pRTx7lJY2ayKCGJvJabO0yCqdWlSDS
cHXqp6YETflUWalMd3OO9ft61J1W1nDa0kP1QQvOEvetbPXdWbAdWBT0Ev2YVFt3
Hb3n26gcTA1vfK/UZTPtgxS5ZRgtsu3SNEBEf8YhaGha0hUshhW9YoXrM380cmBo
JRAjEBqG6qoC6g6kJur9kO9GyDOLMd6yZbMP7pkTpkxHl1ttb1Bb/kGg4vNgYe1M
2ZHQ1VT6VlkFaPWisjDVldsQ7wCaaVyYF7QFR02c8HesMFzCgvn3Qthtu5rP6RCW
iqiur3V2BF0x5KccvVbL19jyXw0MP9cTgiaD1yHnA0HGBGw38/NNkF9r2UB/
-----END CERTIFICATE-----
)CACERT";

// Cliente MQTT e topicos
WiFiClientSecure espTLS;
PubSubClient mqtt(espTLS);
const char* MQTT_TOP_TELEMETRIA = "estufa/" DEVICE_ID "/telemetria";
const char* MQTT_TOP_EVENTO     = "estufa/" DEVICE_ID "/evento";
const char* MQTT_TOP_ONLINE     = "estufa/" DEVICE_ID "/online";
const char* MQTT_TOP_CONFIG_SET   = "estufa/" DEVICE_ID "/config/set";
const char* MQTT_TOP_CONFIG_STATE = "estufa/" DEVICE_ID "/config/state";
const char* MQTT_TOP_COMANDO_SET  = "estufa/" DEVICE_ID "/comando/set";
bool mqttConectado = false;

// Reenvio do backlog: marca d'agua (epoch da ultima linha publicada)
const char* ARQ_MARCA = "/mqttwm.txt";

uint32_t mqttMarca      = 0;
bool     catchupAtivo   = false;
String   catchupArquivo = "";   // arquivo do log em reenvio
uint32_t catchupPos     = 0;    // posicao dentro dele
const int CATCHUP_LOTE = 15;   // linhas reenviadas por iteracao do loop
unsigned long ultimaTentativaMqtt = 0;
const unsigned long INTERVALO_RECONEXAO_MQTT = 10000UL;
long publicacoesMqtt = 0, falhasMqtt = 0;

// Estado e modo dos atuadores em JSON: {"vent":0,...,"manual":{"vent":0,...}}.
String jsonAtuadores() {
  String s = "{";
  for (int i = 0; i < N_ATUADORES; i++) {
    if (i) s += ",";
    s += "\"" + String(atuadores[i].id) + "\":" + String(atuadores[i].ligado ? 1 : 0);
  }
  s += ",\"manual\":{";
  for (int i = 0; i < N_ATUADORES; i++) {
    if (i) s += ",";
    s += "\"" + String(atuadores[i].id) + "\":" + String(atuadores[i].modo == MANUAL ? 1 : 0);
  }
  s += "}}";
  return s;
}

// Mensagem de telemetria (leituras + atuadores) para o MQTT.
String jsonTelemetria(uint32_t epoch, const char* origem) {
  String temp = (sensorPerdido || isnan(ultimaTempValida)) ? "null" : String(ultimaTempValida, 2);
  String uc1 = (umidadeControle1 < 0) ? "null" : String(umidadeControle1);
  String uc2 = (umidadeControle2 < 0) ? "null" : String(umidadeControle2);
  String cr1 = (umidadeControleCorr1 < 0) ? "null" : String(umidadeControleCorr1);
  String cr2 = (umidadeControleCorr2 < 0) ? "null" : String(umidadeControleCorr2);
  String j = "{";
  j += "\"ts\":" + String(epoch) + ",";
  j += "\"temp\":" + temp + ",";
  j += "\"u1\":" + String(umidade1) + ",\"u2\":" + String(umidade2) + ",";
  j += "\"uctrl1\":" + uc1 + ",\"uctrl2\":" + uc2 + ",";
  j += "\"ucorr1\":" + cr1 + ",\"ucorr2\":" + cr2 + ",";
  j += "\"atuadores\":" + jsonAtuadores() + ",";
  j += "\"origem\":\"" + String(origem) + "\"}";
  return j;
}

// Mensagem de evento (tipo, origem e retrato das leituras e atuadores) para o MQTT.
String jsonEvento(uint32_t epoch, const char* evento, const char* origem, const String& extra) {
  String temp = (sensorPerdido || isnan(ultimaTempValida)) ? "null" : String(ultimaTempValida, 2);
  String det = "{";
  det += "\"temp\":" + temp + ",";
  det += "\"umid1\":" + String(umidade1) + ",\"umid2\":" + String(umidade2);
  for (int i = 0; i < N_ATUADORES; i++)
    det += ",\"" + String(atuadores[i].id) + "\":" + String(atuadores[i].ligado ? 1 : 0);
  det += ",\"manual\":{";
  for (int i = 0; i < N_ATUADORES; i++) {
    if (i) det += ",";
    det += "\"" + String(atuadores[i].id) + "\":" + String(atuadores[i].modo == MANUAL ? 1 : 0);
  }
  det += "}";
  if (extra.length()) det += "," + extra;
  det += "}";
  String j = "{";
  j += "\"ts\":" + String(epoch) + ",";
  j += "\"tipo\":\"" + String(evento) + "\",";
  j += "\"detalhe\":" + det + ",";
  j += "\"origem\":\"" + String(origem) + "\"}";
  return j;
}

// Publica a linha no MQTT. Linha gravada no log avanca a marca d'agua (e, durante o reenvio,
// fica para o reenvio); linha que nao foi gravada sai direto, sem mexer na marca.
void publicarLinhaMqtt(char tipo, const char* evento, const char* origem, uint32_t epoch,
                       bool gravado, const String& extra) {
  if (gravado && catchupAtivo) return;
  if (!mqtt.connected()) return;
  bool ok;
  if (tipo == 'E') {
    ok = mqtt.publish(MQTT_TOP_EVENTO, jsonEvento(epoch, evento, origem, extra).c_str());
  } else {
    ok = mqtt.publish(MQTT_TOP_TELEMETRIA, jsonTelemetria(epoch, origem).c_str());
  }
  if (ok) {
    publicacoesMqtt++;
    if (gravado && epoch > mqttMarca) { mqttMarca = epoch; salvarMarca(); }
  } else falhasMqtt++;
}

// Epoch da ultima linha do log (arquivo mais novo).
uint32_t ultimoEpochDoLog() {
  String arqs[MAX_ARQUIVOS_LOG];
  int n = listarArquivosLog(arqs, MAX_ARQUIVOS_LOG);
  return n > 0 ? ultimoEpochArquivo(arqs[n - 1]) : 0;
}

// Carrega a marca d'agua do MQTT; sem arquivo, inicia no ultimo epoch do log.
void carregarMarca() {
  if (fsPronto && LittleFS.exists(ARQ_MARCA)) {
    mqttMarca = 0;
    File f = LittleFS.open(ARQ_MARCA, "r");
    if (f) {
      mqttMarca = (uint32_t) strtoul(f.readStringUntil('\n').c_str(), nullptr, 10);
      f.close();
    }
    Serial.printf("Marca d'agua MQTT: %lu\n", (unsigned long)mqttMarca);
  } else {
    mqttMarca = ultimoEpochDoLog();
    salvarMarca();
    Serial.printf("Marca d'agua MQTT inicializada em %lu (1a vez)\n", (unsigned long)mqttMarca);
  }
}

// Grava a marca d'agua do MQTT (epoch da ultima linha publicada).
void salvarMarca() {
  if (!fsPronto) return;
  File f = LittleFS.open(ARQ_MARCA, "w");
  if (f) { f.println(mqttMarca); f.close(); }
}

// Divide uma linha do CSV em campos; retorna quantos campos foram lidos.
int dividirCsv(const String& linha, String* campos, int maxCampos) {
  int n = 0, ini = 0;
  while (n < maxCampos) {
    int v = linha.indexOf(',', ini);
    if (v < 0) { campos[n++] = linha.substring(ini); break; }
    campos[n++] = linha.substring(ini, v);
    ini = v + 1;
  }
  return n;
}

// Campo numerico do CSV para JSON (vazio vira null).
static String jnum(const String& v) { return v.length() ? v : String("null"); }

// Bloco de atuadores montado a partir dos campos de uma linha do CSV.
String jsonAtuadoresCsv(String* c) {
  String s = "{";
  for (int i = 0; i < N_ATUADORES; i++) {
    if (i) s += ",";
    s += "\"" + String(atuadores[i].id) + "\":" + (c[10 + i].length() ? c[10 + i] : String("0"));
  }
  s += ",\"manual\":{";
  for (int i = 0; i < N_ATUADORES; i++) {
    if (i) s += ",";
    s += "\"" + String(atuadores[i].id) + "\":" + (c[16 + i].length() ? c[16 + i] : String("0"));
  }
  s += "}}";
  return s;
}

// Telemetria montada a partir de uma linha do CSV (mesmo formato de jsonTelemetria).
String jsonTelemetriaCsv(String* c) {
  String j = "{";
  j += "\"ts\":" + c[1] + ",";
  j += "\"temp\":" + jnum(c[3]) + ",";
  j += "\"u1\":" + c[4] + ",\"u2\":" + c[5] + ",";
  j += "\"uctrl1\":" + jnum(c[6]) + ",\"uctrl2\":" + jnum(c[7]) + ",";
  j += "\"ucorr1\":" + jnum(c[8]) + ",\"ucorr2\":" + jnum(c[9]) + ",";
  j += "\"atuadores\":" + jsonAtuadoresCsv(c) + ",";
  j += "\"origem\":\"" + c[23] + "\"}";
  return j;
}

// Evento montado a partir de uma linha do CSV (mesmo formato de jsonEvento).
String jsonEventoCsv(String* c) {
  String det = "{";
  det += "\"temp\":" + jnum(c[3]) + ",";
  det += "\"umid1\":" + c[4] + ",\"umid2\":" + c[5];
  for (int i = 0; i < N_ATUADORES; i++)
    det += ",\"" + String(atuadores[i].id) + "\":" + (c[10 + i].length() ? c[10 + i] : String("0"));
  det += ",\"manual\":{";
  for (int i = 0; i < N_ATUADORES; i++) {
    if (i) det += ",";
    det += "\"" + String(atuadores[i].id) + "\":" + (c[16 + i].length() ? c[16 + i] : String("0"));
  }
  det += "}}";
  String j = "{";
  j += "\"ts\":" + c[1] + ",";
  j += "\"tipo\":\"" + c[22] + "\",";
  j += "\"detalhe\":" + det + ",";
  j += "\"origem\":\"" + c[23] + "\"}";
  return j;
}

// Publica uma linha antiga do CSV se o epoch nao for anterior a marca; retorna o epoch publicado ou 0.
// Linhas no mesmo segundo da marca sao reenviadas (o backend descarta as repetidas pela impressao digital).
uint32_t publicarLinhaCsv(const String& linha) {
  String c[24];
  if (dividirCsv(linha, c, 24) < 24) return 0;
  uint32_t ep = (uint32_t) strtoul(c[1].c_str(), nullptr, 10);
  if (ep == 0 || ep < mqttMarca) return 0;
  if (!mqtt.connected()) return 0;
  char tipo = c[0].length() ? c[0].charAt(0) : 'L';
  bool ok = (tipo == 'E')
              ? mqtt.publish(MQTT_TOP_EVENTO, jsonEventoCsv(c).c_str())
              : mqtt.publish(MQTT_TOP_TELEMETRIA, jsonTelemetriaCsv(c).c_str());
  if (ok) { publicacoesMqtt++; return ep; }
  falhasMqtt++;
  return 0;
}

// Proximo arquivo do log, depois de 'depois', com linhas a partir da marca d'agua ("" se nao houver).
String proximoArquivoCatchup(const String& depois) {
  String arqs[MAX_ARQUIVOS_LOG];
  int n = listarArquivosLog(arqs, MAX_ARQUIVOS_LOG);
  for (int i = 0; i < n; i++) {
    if (depois.length() && arqs[i] <= depois) continue;
    if (ultimoEpochArquivo(arqs[i]) >= mqttMarca) return arqs[i];
  }
  return "";
}

// Encerra o reenvio do backlog.
void finalizarCatchup() {
  catchupAtivo = false;
  catchupArquivo = "";
  catchupPos = 0;
  Serial.printf("Catch-up MQTT concluido | marca=%lu\n", (unsigned long)mqttMarca);
}

// Republica, em lotes e na ordem dos arquivos, as linhas gravadas enquanto o MQTT estava desconectado.
void processarCatchup() {
  if (!fsPronto) { finalizarCatchup(); return; }
  if (!catchupArquivo.length()) {
    catchupArquivo = proximoArquivoCatchup("");
    catchupPos = 0;
    if (!catchupArquivo.length()) { finalizarCatchup(); return; }
  }
  File f = LittleFS.open(catchupArquivo, "r");
  if (!f) { catchupArquivo = ""; return; }
  if (catchupPos > f.size()) catchupPos = 0;
  if (!f.seek(catchupPos)) { f.close(); catchupPos = 0; return; }

  int lidos = 0;
  uint32_t marcaAntes = mqttMarca;
  while (lidos < CATCHUP_LOTE) {
    if (!f.available()) {
      f.close();
      if (mqttMarca != marcaAntes) salvarMarca();
      String prox = proximoArquivoCatchup(catchupArquivo);
      if (!prox.length()) { finalizarCatchup(); return; }
      catchupArquivo = prox;
      catchupPos = 0;
      return;
    }
    String linha = f.readStringUntil('\n');
    catchupPos = f.position();
    lidos++;
    linha.trim();
    if (linha.length() == 0 || linha.startsWith("tipo")) continue;

    uint32_t ep = publicarLinhaCsv(linha);
    if (ep > 0) mqttMarca = ep;

    if (!mqtt.connected()) {
      catchupArquivo = "";
      catchupPos = 0;
      break;
    }
  }
  f.close();
  if (mqttMarca != marcaAntes) salvarMarca();
}

// Publica os parametros efetivos em config/state (retido).
void publicarConfigState() {
  if (!mqtt.connected()) return;
  mqtt.publish(MQTT_TOP_CONFIG_STATE, construirConfigJson().c_str(), true);
}

// Aplica um comando remoto (modo auto/manual e liga/desliga em manual); retorna true se algo mudou.
bool aplicarComandoJson(const String& js) {
  bool mudou = false;
  for (int i = 0; i < N_ATUADORES; i++) {
    String bl = cfgBloco(js, atuadores[i].id);
    if (!bl.length()) continue;
    String modo;
    if (cfgStr(bl, "modo", modo)) {
      setModo(atuadores[i], modo == "manual" ? MANUAL : AUTO);
      mudou = true;
    }
    bool lig;
    if (atuadores[i].modo == MANUAL && cfgBool(bl, "ligado", lig)) {
      setManual(atuadores[i], lig);
      mudou = true;
    }
  }
  return mudou;
}

// Trata as mensagens recebidas: config/set (parametros) e comando/set (acionamento remoto).
void aoReceberMqtt(char* topico, byte* payload, unsigned int tam) {
  String t = String(topico);
  String js; js.reserve(tam + 1);
  for (unsigned int i = 0; i < tam; i++) js += (char)payload[i];

  if (t == MQTT_TOP_CONFIG_SET) {
    Serial.println("config/set recebido da nuvem");
    String antes = construirConfigJson();
    aplicarConfigJson(js);
    if (construirConfigJson() == antes) return;
    salvarConfig();
    publicarConfigState();
    registrarLinha('E', "config_set", "nuvem");

  } else if (t == MQTT_TOP_COMANDO_SET) {
    Serial.println("comando/set recebido da nuvem");
    if (estadoJanela != JAN_INATIVA) abortarJanela();
    if (aplicarComandoJson(js)) {
      aplicarAtuadores();
      registrarLinha('E', "comando", "nuvem");
    }
  }
}

// Conecta ao broker (TLS), publica online, assina config/set e comando/set e agenda o reenvio do backlog.
void conectarMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - ultimaTentativaMqtt < INTERVALO_RECONEXAO_MQTT) return;
  ultimaTentativaMqtt = millis();
  bool ok = mqtt.connect(DEVICE_ID, MQTT_USUARIO, MQTT_SENHA,
                         MQTT_TOP_ONLINE, 1, true, "0");
  if (ok) {
    mqtt.publish(MQTT_TOP_ONLINE, "1", true);
    mqtt.subscribe(MQTT_TOP_CONFIG_SET, 1);
    mqtt.subscribe(MQTT_TOP_COMANDO_SET, 1);
    publicarConfigState();
    bool novaConexao = !mqttConectado;
    mqttConectado = true;
    if (novaConexao) {
      catchupAtivo = true; catchupArquivo = ""; catchupPos = 0;
      registrarLinha('E', "mqtt_on", "sistema");
    }
  } else {
    mqttConectado = false;
  }
}
