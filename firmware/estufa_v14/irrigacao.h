// Irrigacao por zona: doses por volume, agendamento, limiar e protecao das valvulas.
#pragma once

#include <Arduino.h>

enum EstadoIrrig { IRRIG_OCIOSO, IRRIG_ATIVA, IRRIG_ESPERA };

struct ZonaIrrig {
  int  indiceAtuador;
  EstadoIrrig estado;
  int  confirmacoes;
  unsigned long inicioIrrigacao;
  unsigned long fimIrrigacao;
  unsigned long bloqueadoAte;
  long totalIrrigacoes;
  long ultimaMedicaoContada;         // ultima medicao contada no modo limiar
  unsigned long duracaoAtual;        // duracao da dose em curso (ms)
  const char*   origemDose;          // origem da dose em curso
  unsigned long inicioAgendamento;   // referencia de tempo do agendamento
};

const unsigned long TICK_IRRIGACAO = 60000UL;   // intervalo de avaliacao da irrigacao (ms)

extern ZonaIrrig zonas[2];
extern unsigned long ultimoTickIrrig;

unsigned long duracaoDose(int iz);
bool solosaturado(int iz);
void iniciarDose(int iz, unsigned long duracao, const char* origem);
void processarIrrigacao(int iz);
void protegerValvulas();
void marcarAgendamento(int iz);
void encerrarDose(int iz);
void restaurarIrrigacao();
bool haDoseParaRetomar();
