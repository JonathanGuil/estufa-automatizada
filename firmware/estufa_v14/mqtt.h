// Comunicacao com a VM por MQTT/TLS: publicacao, reenvio do backlog, config/set e comando/set.
#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

extern const char* CA_CERT;
extern WiFiClientSecure espTLS;
extern PubSubClient mqtt;
extern bool mqttConectado;
extern bool catchupAtivo;
extern long publicacoesMqtt;
extern uint32_t mqttMarca;

String   jsonAtuadores();
String   jsonTelemetria(uint32_t epoch, const char* origem);
String   jsonEvento(uint32_t epoch, const char* evento, const char* origem,
                    const String& extra = String());
void     publicarLinhaMqtt(char tipo, const char* evento, const char* origem, uint32_t epoch,
                           bool gravado, const String& extra);
uint32_t ultimoEpochDoLog();
void     carregarMarca();
void     salvarMarca();
int      dividirCsv(const String& linha, String* campos, int maxCampos);
String   jsonAtuadoresCsv(String* c);
String   jsonTelemetriaCsv(String* c);
String   jsonEventoCsv(String* c);
uint32_t publicarLinhaCsv(const String& linha);
void     processarCatchup();
void     publicarConfigState();
bool     aplicarComandoJson(const String& js);
void     aoReceberMqtt(char* topico, byte* payload, unsigned int tam);
void     conectarMqtt();
