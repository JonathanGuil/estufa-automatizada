// Sensores: temperatura (DS18B20) e umidade do solo (capacitivos).

#include <Arduino.h>
#include <math.h>
#include "sensores.h"
#include "parametros.h"

// Temperatura (DS18B20)
const int PINO_DS18B20 = 4;
OneWire oneWire(PINO_DS18B20);
DallasTemperature ds(&oneWire);

// Umidade do solo
const int AMOSTRAS_UMIDADE = 11;
int umidade1 = 0, umidade2 = 0;
unsigned long ultimaLeituraUmidade = 0;

float ultimaTempValida = NAN;
bool  sensorPerdido = true;
int   falhasSeguidas = 0;
const int MAX_FALHAS = 4;
String statusSensor = "aguardando...";
bool conversaoPendente = false;
unsigned long tConversao = 0, ultimaLeitura = 0;

// Compensa a influencia da temperatura na leitura de umidade.
int corrigirLeitura(int bruto) {
  if (bruto < 0) return -1;
  if (sensorPerdido || isnan(ultimaTempValida)) return bruto;
  long c = bruto - lround((double)kTermico * (ultimaTempValida - tempRefUmid));
  if (c < 0) c = 0;
  return (int)c;
}

// Converte a leitura corrigida em % de deplecao, pela calibracao da zona.
int adcParaPercent(int adcCorr, int iz) {
  if (adcCorr < 0) return -1;
  int u = refUmido[iz], s = refSeco[iz];
  if (s == u) return -1;
  long p = (long)(adcCorr - u) * 100 / (s - u);
  if (p < 0) p = 0; if (p > 100) p = 100;
  return (int)p;
}

// Mediana de 11 leituras do sensor de umidade.
int lerUmidade(int pino) {
  int v[AMOSTRAS_UMIDADE];
  for (int i = 0; i < AMOSTRAS_UMIDADE; i++) { v[i] = analogRead(pino); delay(2); }
  for (int i = 1; i < AMOSTRAS_UMIDADE; i++) {
    int c = v[i], j = i - 1;
    while (j >= 0 && v[j] > c) { v[j + 1] = v[j]; j--; }
    v[j + 1] = c;
  }
  return v[AMOSTRAS_UMIDADE / 2];
}

// Valida a leitura do DS18B20 e marca o sensor como perdido apos falhas seguidas.
void processarLeitura(float t) {
  bool ruim = (t == DEVICE_DISCONNECTED_C || t == -127.0f || t == 85.0f);
  if (!ruim) {
    ultimaTempValida = t; falhasSeguidas = 0;
    sensorPerdido = false; statusSensor = "OK";
  } else {
    falhasSeguidas++;
    if (falhasSeguidas >= MAX_FALHAS) {
      sensorPerdido = true;
      statusSensor = "sensor perdido (" + String(falhasSeguidas) + " falhas)";
    } else {
      statusSensor = "leitura ruim ignorada (" + String(falhasSeguidas) + "/" +
                     String(MAX_FALHAS) + ")";
    }
  }
}
