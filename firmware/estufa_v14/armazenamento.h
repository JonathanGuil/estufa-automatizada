// Log CSV no LittleFS: um arquivo por dia, retencao e limpeza.
#pragma once

#include <Arduino.h>

extern const char* DIR_LOG;
extern const int   LOG_DIAS_MAX;
extern bool fsPronto, logCheio;
extern long linhasLog, descartadosSemHora, falhasGravacao;
extern String ultimoRegistro;
extern unsigned long ultimoEpochGravado;
extern unsigned long ultimoLog, intervaloLog;

const int MAX_ARQUIVOS_LOG = 40;

int      listarArquivosLog(String* caminhos, int maximo);
uint32_t ultimoEpochArquivo(const String& caminho);
void     contarLinhas();
void     iniciarFs();
void     limparAteEpoch(unsigned long corte);
void     limparTudo();
void     registrarLinha(char tipo, const char* evento, const char* origem,
                        const String& extra = String());
void     agendarEvento(const char* tipo, const char* origem, const String& extra = String());
void     emitirPendentes();
void     setIntervaloLog(unsigned long ms);
size_t   tamanhoLog();
String   resumoArquivosLog();
