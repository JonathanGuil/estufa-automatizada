// Estufa automatizada - controlador ESP32 (TCC, Univates).
// Este arquivo tem apenas setup() e loop(); cada parte do sistema esta em um modulo (.h/.cpp).
// Saidas dos reles: HIGH = ligado, LOW = desligado.

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <RTClib.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "hora.h"
#include "rede.h"
#include "armazenamento.h"
#include "configuracao.h"
#include "mqtt.h"
#include "sensores.h"
#include "atuadores.h"
#include "parametros.h"
#include "controle.h"
#include "irrigacao.h"
#include "janela.h"
#include "web.h"

const uint32_t WATCHDOG_S = 120;   // reinicia o ESP32 se o loop ficar parado por mais que isto

// Liga o watchdog de tarefa para o loop (API do core 3.x ou 2.x).
void iniciarWatchdog() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg = { WATCHDOG_S * 1000, 0, true };
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
#else
  esp_task_wdt_init(WATCHDOG_S, true);
#endif
  esp_task_wdt_add(NULL);
}

void setup() {
  relesDesligados();
  Serial.begin(115200);
  setenv("TZ", TZ_ESTUFA, 1); tzset();
  Serial.println("Firmware v" FIRMWARE_VERSAO);

  iniciarRtc();
  Serial.printf("RTC: %s | %s\n", fonteHora.c_str(), horaFormatada().c_str());
  iniciarFs();

  ds.begin(); ds.setWaitForConversion(false);

  conectarWiFi();
  if (wifiConectado) { sincronizarNtp(); Serial.print("IP: "); Serial.println(WiFi.localIP()); }

  MDNS.begin(OTA_HOSTNAME);
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_SENHA);
  ArduinoOTA.onStart([]() { relesDesligados(); });
  ArduinoOTA.onProgress([](unsigned int, unsigned int) { esp_task_wdt_reset(); });
  ArduinoOTA.begin();

  espTLS.setCACert(CA_CERT);
  espTLS.setHandshakeTimeout(10);       // limita o bloqueio se o handshake TLS travar
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);             // telemetria passa de 256 bytes (padrao da PubSubClient)
  mqtt.setKeepAlive(60);
  mqtt.setCallback(aoReceberMqtt);

  server.on("/", handleRoot);
  server.on("/estado", handleEstado);
  server.on("/cmd", handleCmd);
  server.on("/cfg", handleCfg);
  server.on("/dose", handleDose);
  server.on("/log", handleLogDownload);
  server.on("/log/registrar", handleLogRegistrar);
  server.on("/log/limpar", handleLogLimpar);
  server.on("/log/limpar_ate", handleLogLimparAte);
  server.on("/medir", handleMedirAgora);
  server.begin();
  MDNS.addService("http", "tcp", 80);

  registrarLinha('E', "boot", "sistema", "\"fw\":\"" FIRMWARE_VERSAO "\"");

  // agendamento continua de onde parou; dose interrompida e retomada logo apos o boot
  restaurarIrrigacao();
  if (haDoseParaRetomar()) ultimoTickIrrig = millis() - TICK_IRRIGACAO + 15000UL;

  // primeira medicao de controle cerca de 1 min apos o boot
  ultimaJanela = millis() - intervaloJanela + 60000UL;

  iniciarWatchdog();
}

void loop() {
  esp_task_wdt_reset();
  ArduinoOTA.handle();
  server.handleClient();
  verificarNtp();
  verificarWifi();

  // a reconexao MQTT (que pode bloquear no handshake TLS) espera o fim da janela de medicao
  if (wifiConectado) {
    if (mqtt.connected()) {
      mqtt.loop();
      if (catchupAtivo && estadoJanela == JAN_INATIVA) processarCatchup();
    } else if (estadoJanela == JAN_INATIVA) { mqttConectado = false; conectarMqtt(); }
  }

  if (!conversaoPendente && millis() - ultimaLeitura >= INTERVALO_LEITURA) {
    ds.requestTemperatures(); tConversao = millis(); conversaoPendente = true;
  }
  if (conversaoPendente && millis() - tConversao >= 750) {
    float t = ds.getTempCByIndex(0);
    conversaoPendente = false; ultimaLeitura = millis();
    processarLeitura(t);
  }

  if (estadoJanela == JAN_INATIVA &&
      millis() - ultimaLeituraUmidade >= INTERVALO_UMIDADE) {
    ultimaLeituraUmidade = millis();
    umidade1 = lerUmidade(PINO_UMIDADE_1);
    umidade2 = lerUmidade(PINO_UMIDADE_2);
  }

  processarJanela();

  // irrigacao avaliada a cada minuto; o fim da dose e verificado a cada volta
  if (millis() - ultimoTickIrrig >= TICK_IRRIGACAO) {
    ultimoTickIrrig = millis();
    processarIrrigacao(0);
    processarIrrigacao(1);
  }
  for (int iz = 0; iz < 2; iz++)
    if (zonas[iz].estado == IRRIG_ATIVA) processarIrrigacao(iz);

  // durante a janela de medicao os atuadores ficam sob controle da janela
  if (estadoJanela == JAN_INATIVA) {
    aplicarAtuadores();
    verificarAquecedor();
  }

  if (millis() - ultimoLog >= intervaloLog) {
    ultimoLog = millis();
    registrarLinha('L', "-", "periodico");
  }

  emitirPendentes();
}
