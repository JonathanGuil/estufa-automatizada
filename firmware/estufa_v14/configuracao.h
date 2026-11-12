// Parametros em JSON: leitura, aplicacao, persistencia em /config.json.
#pragma once

#include <Arduino.h>

String cfgBloco(const String& js, const String& chave);
bool   cfgNum(const String& js, const String& chave, float& out);
bool   cfgPar(const String& js, const String& chave, float& a, float& b);
bool   cfgBool(const String& js, const String& chave, bool& out);
bool   cfgStr(const String& js, const String& chave, String& out);
void   aplicarConfigJson(const String& js);
String construirConfigJson();
void   salvarConfig();
void   carregarConfig();
