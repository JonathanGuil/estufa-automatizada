// Parametros de controle ajustaveis em tempo de execucao (painel local e nuvem).

#include <Arduino.h>
#include "parametros.h"

// Fotoperiodo: horas em que o LED liga e desliga
int ledHoraLiga = 6, ledHoraDesliga = 21;

// Setpoints termicos (C): liga e desliga, de dia e de noite
float aquecLigaDia = 20.0, aquecDesligaDia = 22.0;
float ventLigaDia  = 28.0, ventDesligaDia  = 26.0;
float exausLigaDia = 31.0, exausDesligaDia = 29.0;

float aquecLigaNoite = 13.5, aquecDesligaNoite = 15.5;
float ventLigaNoite  = 26.0, ventDesligaNoite  = 24.0;
float exausLigaNoite = 29.0, exausDesligaNoite = 27.0;

// Ciclo de circulacao: pulsos do ventilador numa janela horaria, independentes da temperatura
bool          circHabilitado      = true;
int           circHoraIni         = 10;          // hora de inicio (inclusiva)
int           circHoraFim         = 17;          // hora de fim (exclusiva)
unsigned long circLigaMs          = 300000UL;    // ms ligado em cada pulso
unsigned long circDeslMs          = 1500000UL;   // ms desligado entre pulsos
bool          circAcionaExaustor  = false;       // true: o pulso liga tambem o exaustor

// Irrigacao por zona
Estrategia estrategia[2] = { ESTR_AGENDADO, ESTR_AGENDADO };   // agendado (volume/tempo) ou limiar (sensor)

int vazao[2]      = { 70, 120 };    // mL/min medidos por zona
int doseVolume[2] = { 200, 200 };   // mL por dose

unsigned long intervaloAgendado[2] = { 86400000UL, 86400000UL };   // ms entre doses agendadas

bool doseManualReiniciaIntervalo = true;   // true: dose manual reinicia a contagem do agendamento

int limiarIrrigar[2] = { 1400, 1400 };   // ADC (leitura corrigida) para irrigar no modo limiar

// Supervisao por umidade: pula a dose agendada se o solo ja estiver umido
bool supervisaoUmidade = true;
int  limiarSaturado[2] = { 900, 900 };   // ADC abaixo do qual o solo e considerado umido

// Compensacao termica e calibracao dos sensores de umidade
float kTermico    = 26.0;   // ADC por grau Celsius
float tempRefUmid = 20.0;   // temperatura de referencia da compensacao (C)

int refUmido[2] = { 800, 800 };     // ADC corrigido de capacidade de campo (0%)
int refSeco[2]  = { 1600, 1600 };   // ADC corrigido de solo seco (100%)

// Tempos de irrigacao
unsigned long intervaloMinimoIrrig = 3600000UL;   // ms minimos entre doses
unsigned long janelaCega = 1800000UL;             // ms sem decidir apos uma dose
int confirmacoesNecessarias = 2;                  // medicoes seguidas acima do limiar (modo limiar)
