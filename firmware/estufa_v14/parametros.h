// Parametros de controle ajustaveis em tempo de execucao (painel local e nuvem).
#pragma once

#include <Arduino.h>

enum Estrategia { ESTR_AGENDADO, ESTR_LIMIAR };

// Limites de seguranca da irrigacao
const unsigned long TEMPO_MAX_VALVULA = 300000UL;     // corte forcado da valvula (ms)
const int SALTO_MAX_UMIDADE = 300;                    // variacao maxima de uma janela (ADC)
const unsigned long BLOQUEIO_APOS_SALTO = 600000UL;   // ms de bloqueio apos um salto

extern int ledHoraLiga, ledHoraDesliga;

extern float aquecLigaDia, aquecDesligaDia;
extern float ventLigaDia,  ventDesligaDia;
extern float exausLigaDia, exausDesligaDia;
extern float aquecLigaNoite, aquecDesligaNoite;
extern float ventLigaNoite,  ventDesligaNoite;
extern float exausLigaNoite, exausDesligaNoite;

extern bool          circHabilitado;
extern int           circHoraIni, circHoraFim;
extern unsigned long circLigaMs, circDeslMs;
extern bool          circAcionaExaustor;

extern Estrategia    estrategia[2];
extern int           vazao[2];
extern int           doseVolume[2];
extern unsigned long intervaloAgendado[2];
extern bool          doseManualReiniciaIntervalo;
extern int           limiarIrrigar[2];
extern bool          supervisaoUmidade;
extern int           limiarSaturado[2];
extern float         kTermico, tempRefUmid;
extern int           refUmido[2], refSeco[2];
extern unsigned long intervaloMinimoIrrig, janelaCega;
extern int           confirmacoesNecessarias;
