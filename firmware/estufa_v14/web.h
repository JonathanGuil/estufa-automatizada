// Painel HTTP local e suas rotas.
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "irrigacao.h"

extern WebServer server;

String nomeEstado(EstadoIrrig e);
String estadoJson();
void handleCmd();
void handleCfg();
void handleEstado();
void handleMedirAgora();
void handleDose();
void handleLogDownload();
void handleLogRegistrar();
void handleLogLimpar();
void handleLogLimparAte();
void handleRoot();
