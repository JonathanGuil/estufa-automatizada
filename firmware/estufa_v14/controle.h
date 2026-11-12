// Logica de controle: decisoes automaticas, circulacao e supervisao do aquecedor.
#pragma once

#include <Arduino.h>

extern bool   aquecNoLimite;
extern String textoAlerta;

void acompanharAquecedor(bool ligou);
void verificarAquecedor();
bool autoLed();
bool autoVentilador();
bool autoExaustor();
bool circulacaoAtiva();
bool autoAquecedor();
void aplicarAtuadores();
