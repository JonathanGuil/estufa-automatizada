// Atuadores (reles): estado, modo e escrita nos pinos.
#pragma once

#include <Arduino.h>

enum Modo { AUTO, MANUAL };

struct Atuador {
  const char* id;
  const char* nome;
  int   pino;
  Modo  modo;
  bool  manualLigado;
  bool  ligado;
};

const int AT_VENT = 0, AT_AQUEC = 1, AT_EXAUS = 2, AT_LED = 3,
          AT_VALV1 = 4, AT_VALV2 = 5;

const int N_ATUADORES = 6;
const int LIGADO = HIGH, DESLIGADO = LOW;

extern Atuador atuadores[];
extern bool suprimirEventos;

void     relesDesligados();
Atuador* buscarAtuador(const String& id);
void     escrever(Atuador& a, bool alvo, const char* origem);
void     setModo(Atuador& a, Modo m);
void     setManual(Atuador& a, bool ligado);
