// Atuadores (reles): estado, modo e escrita nos pinos.

#include <Arduino.h>
#include "atuadores.h"
#include "armazenamento.h"
#include "controle.h"
#include "irrigacao.h"

Atuador atuadores[] = {
  { "vent",  "Ventilador",     14, AUTO, false, false },
  { "aquec", "Aquecedor",      27, AUTO, false, false },
  { "exaus", "Exaustor",       13, AUTO, false, false },
  { "led",   "Painel LED",     18, AUTO, false, false },
  { "valv1", "Valvula Zona 1", 25, AUTO, false, false },
  { "valv2", "Valvula Zona 2", 26, AUTO, false, false }
};

bool suprimirEventos = false;   // true durante a janela de medicao: nao registra eventos

// Configura os pinos dos reles como saida e desliga todos.
void relesDesligados() {
  for (int i = 0; i < N_ATUADORES; i++) {
    digitalWrite(atuadores[i].pino, DESLIGADO);
    pinMode(atuadores[i].pino, OUTPUT);
    digitalWrite(atuadores[i].pino, DESLIGADO);
    atuadores[i].ligado = false;
  }
}

// Atuador pelo id, ou nullptr.
Atuador* buscarAtuador(const String& id) {
  for (int i = 0; i < N_ATUADORES; i++)
    if (id == atuadores[i].id) return &atuadores[i];
  return nullptr;
}

// Liga ou desliga um atuador e registra o evento (exceto durante a janela de medicao).
void escrever(Atuador& a, bool alvo, const char* origem) {
  if (alvo != a.ligado) {
    a.ligado = alvo;
    digitalWrite(a.pino, alvo ? LIGADO : DESLIGADO);
    if (&a == &atuadores[AT_AQUEC]) acompanharAquecedor(alvo);
    if (!suprimirEventos) registrarLinha('E', a.id, origem);
  }
}

// Muda o modo de um atuador (auto/manual).
void setModo(Atuador& a, Modo m) {
  if (m == a.modo) return;
  if (m == MANUAL) a.manualLigado = a.ligado;
  a.modo = m;
  for (int iz = 0; iz < 2; iz++)
    if (&a == &atuadores[zonas[iz].indiceAtuador]) {
      zonas[iz].estado = IRRIG_OCIOSO;
      zonas[iz].confirmacoes = 0;
      encerrarDose(iz);
    }
}

// Define o estado desejado de um atuador em modo manual.
void setManual(Atuador& a, bool ligado) { a.manualLigado = ligado; }
