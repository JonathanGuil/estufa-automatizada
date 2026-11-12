// Sensores: temperatura (DS18B20) e umidade do solo (capacitivos).
#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Umidade do solo
const int PINO_UMIDADE_1 = 34;
const int PINO_UMIDADE_2 = 35;

const unsigned long INTERVALO_UMIDADE = 1000;   // ms entre leituras de umidade

const unsigned long INTERVALO_LEITURA = 2000;   // ms entre leituras de temperatura

extern DallasTemperature ds;
extern int umidade1, umidade2;
extern unsigned long ultimaLeituraUmidade;
extern float ultimaTempValida;
extern bool  sensorPerdido;
extern String statusSensor;
extern bool conversaoPendente;
extern unsigned long tConversao, ultimaLeitura;

int  corrigirLeitura(int bruto);
int  adcParaPercent(int adcCorr, int iz);
int  lerUmidade(int pino);
void processarLeitura(float t);
