// Janela de medicao: desliga as cargas, mede a umidade de controle e restaura.

#include <Arduino.h>
#include "janela.h"
#include "atuadores.h"
#include "sensores.h"
#include "hora.h"
#include "irrigacao.h"
#include "parametros.h"
#include "armazenamento.h"

// Estado e tempos da janela de medicao
EstadoJanela estadoJanela = JAN_INATIVA;
unsigned long intervaloJanela = 3600000UL;   // ms entre janelas de medicao
unsigned long tempoAcomodacao = 6000UL;      // ms de espera apos desligar as cargas
const unsigned long INTERVALO_AMOSTRA = 2000UL;
const int AMOSTRAS_JANELA = 10;

unsigned long ultimaJanela = 0, marcoJanela = 0, ultimaAmostraJanela = 0;
int amostrasColetadas = 0;
int bufJanela1[AMOSTRAS_JANELA], bufJanela2[AMOSTRAS_JANELA];
bool estadoSalvo[6];

// Leituras de controle (bruta e corrigida por temperatura)
int umidadeControle1 = -1, umidadeControle2 = -1;
int umidadeControleCorr1 = -1, umidadeControleCorr2 = -1;
int ctrlAnterior1 = -1, ctrlAnterior2 = -1;
unsigned long ultimoInstanteControle = 0;
String horaUltimaMedicao = "nenhuma";
long totalMedicoes = 0;

// Bloqueia a zona quando a leitura de controle varia mais do que o fisicamente possivel.
void verificarSaltoControle() {
  bool houveIrrigacao = false;
  for (int i = 0; i < 2; i++)
    if (zonas[i].estado != IRRIG_OCIOSO) houveIrrigacao = true;

  unsigned long agora = millis();
  unsigned long gap = (ultimoInstanteControle > 0) ? (agora - ultimoInstanteControle)
                                                    : intervaloJanela;
  double fator = (double)gap / (double)intervaloJanela;
  if (fator < 1.0) fator = 1.0;
  int limiarSalto = (int)(SALTO_MAX_UMIDADE * fator);

  if (!houveIrrigacao) {
    if (ctrlAnterior1 >= 0 && abs(umidadeControleCorr1 - ctrlAnterior1) > limiarSalto) {
      zonas[0].bloqueadoAte = agora + BLOQUEIO_APOS_SALTO;
      zonas[0].confirmacoes = 0;
      registrarLinha('E', "salto_umid1", "seguranca");
    }
    if (ctrlAnterior2 >= 0 && abs(umidadeControleCorr2 - ctrlAnterior2) > limiarSalto) {
      zonas[1].bloqueadoAte = agora + BLOQUEIO_APOS_SALTO;
      zonas[1].confirmacoes = 0;
      registrarLinha('E', "salto_umid2", "seguranca");
    }
  }
  ctrlAnterior1 = umidadeControleCorr1;
  ctrlAnterior2 = umidadeControleCorr2;
  ultimoInstanteControle = agora;
}

// Mediana de um vetor (ordena o vetor no lugar).
int medianaBuffer(int* v, int n) {
  for (int i = 1; i < n; i++) {
    int c = v[i], j = i - 1;
    while (j >= 0 && v[j] > c) { v[j + 1] = v[j]; j--; }
    v[j + 1] = c;
  }
  return v[n / 2];
}

// Interrompe a janela de medicao e restaura os atuadores.
void abortarJanela() {
  if (estadoJanela == JAN_INATIVA) return;
  suprimirEventos = true;
  for (int i = 0; i < N_ATUADORES; i++)
    escrever(atuadores[i], estadoSalvo[i], "sistema");
  suprimirEventos = false;
  estadoJanela = JAN_INATIVA;
  ultimaJanela = millis();
}

// Inicia a janela de medicao: guarda o estado e desliga as cargas.
void iniciarJanela() {
  for (int i = 0; i < 2; i++)
    if (zonas[i].estado == IRRIG_ATIVA) { ultimaJanela = millis(); return; }

  for (int i = 0; i < N_ATUADORES; i++) estadoSalvo[i] = atuadores[i].ligado;

  suprimirEventos = true;
  for (int i = 0; i < N_ATUADORES; i++) escrever(atuadores[i], false, "sistema");
  suprimirEventos = false;

  estadoJanela = JAN_ACOMODANDO;
  marcoJanela = millis();
  amostrasColetadas = 0;
}

// Maquina de estados da janela de medicao (acomodar, coletar, restaurar).
void processarJanela() {
  switch (estadoJanela) {

    case JAN_INATIVA:
      if (millis() - ultimaJanela >= intervaloJanela) iniciarJanela();
      break;

    case JAN_ACOMODANDO:
      if (millis() - marcoJanela >= tempoAcomodacao) {
        estadoJanela = JAN_COLETANDO;
        ultimaAmostraJanela = millis() - INTERVALO_AMOSTRA;
      }
      break;

    case JAN_COLETANDO:
      if (millis() - ultimaAmostraJanela >= INTERVALO_AMOSTRA) {
        ultimaAmostraJanela = millis();
        bufJanela1[amostrasColetadas] = lerUmidade(PINO_UMIDADE_1);
        bufJanela2[amostrasColetadas] = lerUmidade(PINO_UMIDADE_2);
        amostrasColetadas++;
        if (amostrasColetadas >= AMOSTRAS_JANELA) estadoJanela = JAN_RESTAURANDO;
      }
      break;

    case JAN_RESTAURANDO:
      umidadeControle1 = medianaBuffer(bufJanela1, AMOSTRAS_JANELA);
      umidadeControle2 = medianaBuffer(bufJanela2, AMOSTRAS_JANELA);
      umidadeControleCorr1 = corrigirLeitura(umidadeControle1);
      umidadeControleCorr2 = corrigirLeitura(umidadeControle2);
      horaUltimaMedicao = horaFormatada();
      totalMedicoes++;

      suprimirEventos = true;
      for (int i = 0; i < N_ATUADORES; i++)
        escrever(atuadores[i], estadoSalvo[i], "sistema");
      suprimirEventos = false;

      estadoJanela = JAN_INATIVA;
      ultimaJanela = millis();

      registrarLinha('M', "medicao", "janela");
      verificarSaltoControle();
      break;
  }
}
