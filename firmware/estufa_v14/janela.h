// Janela de medicao: desliga as cargas, mede a umidade de controle e restaura.
#pragma once

#include <Arduino.h>

enum EstadoJanela { JAN_INATIVA, JAN_ACOMODANDO, JAN_COLETANDO, JAN_RESTAURANDO };

extern EstadoJanela  estadoJanela;
extern unsigned long intervaloJanela, tempoAcomodacao;
extern unsigned long ultimaJanela;
extern int    umidadeControle1, umidadeControle2;
extern int    umidadeControleCorr1, umidadeControleCorr2;
extern String horaUltimaMedicao;
extern long   totalMedicoes;

void verificarSaltoControle();
int  medianaBuffer(int* v, int n);
void abortarJanela();
void iniciarJanela();
void processarJanela();
