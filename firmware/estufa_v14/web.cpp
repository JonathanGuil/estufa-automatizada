// Painel HTTP local e suas rotas.

#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <math.h>
#include "web.h"
#include "hora.h"
#include "rede.h"
#include "mqtt.h"
#include "sensores.h"
#include "janela.h"
#include "controle.h"
#include "armazenamento.h"
#include "parametros.h"
#include "irrigacao.h"
#include "atuadores.h"
#include "configuracao.h"
#include "config.h"
#include <esp_task_wdt.h>

WebServer server(80);

// Nome do estado da irrigacao para o painel.
String nomeEstado(EstadoIrrig e) {
  if (e == IRRIG_ATIVA)  return "irrigando";
  if (e == IRRIG_ESPERA) return "janela cega";
  return "monitorando";
}

// Estado completo do sistema em JSON para o painel local.
String estadoJson() {
  String tempJson = (sensorPerdido || isnan(ultimaTempValida))
                      ? String("null") : String(ultimaTempValida, 2);
  int pct1 = adcParaPercent(umidadeControleCorr1, 0);
  int pct2 = adcParaPercent(umidadeControleCorr2, 1);

  String j = "{";
  j += "\"fw\":\"" FIRMWARE_VERSAO "\",";
  j += "\"hora\":\"" + horaFormatada() + "\",";
  j += "\"fonteHora\":\"" + fonteHora + "\",";
  j += "\"periodo\":\"" + String(ehDia() ? "dia" : "noite") + "\",";
  j += "\"wifi\":" + String(wifiConectado ? "true" : "false") + ",";
  j += "\"ip\":\"" + (wifiConectado ? WiFi.localIP().toString() : String("--")) + "\",";
  j += "\"quedasWifi\":" + String(quedasWifi) + ",";
  j += "\"mqtt\":" + String(mqtt.connected() ? "true" : "false") + ",";
  j += "\"mqttPub\":" + String(publicacoesMqtt) + ",";
  j += "\"temp\":" + tempJson + ",";
  j += "\"status\":\"" + statusSensor + "\",";
  j += "\"umid1\":" + String(umidade1) + ",";
  j += "\"umid2\":" + String(umidade2) + ",";
  j += "\"ctrl1\":" + String(umidadeControle1) + ",";
  j += "\"ctrl2\":" + String(umidadeControle2) + ",";
  j += "\"corr1\":" + String(umidadeControleCorr1) + ",";
  j += "\"corr2\":" + String(umidadeControleCorr2) + ",";
  j += "\"pct1\":" + String(pct1) + ",";
  j += "\"pct2\":" + String(pct2) + ",";
  j += "\"janelaAtiva\":" + String(estadoJanela != JAN_INATIVA ? "true" : "false") + ",";
  j += "\"ultimaMedicao\":\"" + horaUltimaMedicao + "\",";
  j += "\"totalMedicoes\":" + String(totalMedicoes) + ",";
  j += "\"proximaMedicao\":" +
       String((millis() - ultimaJanela >= intervaloJanela) ? 0
              : (intervaloJanela - (millis() - ultimaJanela)) / 60000) + ",";
  j += "\"alerta\":\"" + textoAlerta + "\",";
  j += "\"aquecLimite\":" + String(aquecNoLimite ? "true" : "false") + ",";
  j += "\"logLinhas\":" + String(linhasLog) + ",";
  j += "\"logBytes\":" + String((unsigned long)tamanhoLog()) + ",";
  j += "\"logCheio\":" + String(logCheio ? "true" : "false") + ",";
  j += "\"logArquivos\":\"" + resumoArquivosLog() + "\",";
  j += "\"falhasLog\":" + String(falhasGravacao) + ",";
  j += "\"ultimoEpoch\":" + String(ultimoEpochGravado) + ",";
  j += "\"fsTotal\":" + String((unsigned long)(fsPronto ? LittleFS.totalBytes() : 0)) + ",";
  j += "\"fsUsado\":" + String((unsigned long)(fsPronto ? LittleFS.usedBytes() : 0)) + ",";
  j += "\"descartados\":" + String(descartadosSemHora) + ",";
  j += "\"intervaloLog\":" + String(intervaloLog / 1000) + ",";
  j += "\"ultimoRegistro\":\"" + ultimoRegistro + "\",";

  j += "\"cfg\":{";
  j += "\"ledLiga\":" + String(ledHoraLiga) + ",\"ledDesliga\":" + String(ledHoraDesliga) + ",";
  j += "\"aqLigaD\":" + String(aquecLigaDia,1) + ",\"aqDeslD\":" + String(aquecDesligaDia,1) + ",";
  j += "\"vtLigaD\":" + String(ventLigaDia,1) + ",\"vtDeslD\":" + String(ventDesligaDia,1) + ",";
  j += "\"exLigaD\":" + String(exausLigaDia,1) + ",\"exDeslD\":" + String(exausDesligaDia,1) + ",";
  j += "\"aqLigaN\":" + String(aquecLigaNoite,1) + ",\"aqDeslN\":" + String(aquecDesligaNoite,1) + ",";
  j += "\"vtLigaN\":" + String(ventLigaNoite,1) + ",\"vtDeslN\":" + String(ventDesligaNoite,1) + ",";
  j += "\"exLigaN\":" + String(exausLigaNoite,1) + ",\"exDeslN\":" + String(exausDesligaNoite,1) + ",";
  j += "\"circEn\":" + String(circHabilitado ? 1 : 0) + ",";
  j += "\"circIni\":" + String(circHoraIni) + ",\"circFim\":" + String(circHoraFim) + ",";
  j += "\"circOn\":" + String(circLigaMs / 60000.0, 1) + ",\"circOff\":" + String(circDeslMs / 60000.0, 1) + ",";
  j += "\"circEx\":" + String(circAcionaExaustor ? 1 : 0) + ",";
  j += "\"estr1\":\"" + String(estrategia[0]==ESTR_AGENDADO?"agendado":"limiar") + "\",";
  j += "\"estr2\":\"" + String(estrategia[1]==ESTR_AGENDADO?"agendado":"limiar") + "\",";
  j += "\"vaz1\":" + String(vazao[0]) + ",\"vaz2\":" + String(vazao[1]) + ",";
  j += "\"dose1\":" + String(doseVolume[0]) + ",\"dose2\":" + String(doseVolume[1]) + ",";
  j += "\"agend1\":" + String(intervaloAgendado[0]/3600000UL) + ",";
  j += "\"agend2\":" + String(intervaloAgendado[1]/3600000UL) + ",";
  j += "\"lim1\":" + String(limiarIrrigar[0]) + ",\"lim2\":" + String(limiarIrrigar[1]) + ",";
  j += "\"sat1\":" + String(limiarSaturado[0]) + ",\"sat2\":" + String(limiarSaturado[1]) + ",";
  j += "\"superv\":" + String(supervisaoUmidade ? 1 : 0) + ",";
  j += "\"kterm\":" + String(kTermico,1) + ",\"tref\":" + String(tempRefUmid,1) + ",";
  j += "\"refU1\":" + String(refUmido[0]) + ",\"refU2\":" + String(refUmido[1]) + ",";
  j += "\"refS1\":" + String(refSeco[0]) + ",\"refS2\":" + String(refSeco[1]) + ",";
  j += "\"intMin\":" + String(intervaloMinimoIrrig/60000) + ",";
  j += "\"jCega\":" + String(janelaCega/60000) + ",";
  j += "\"conf\":" + String(confirmacoesNecessarias) + ",";
  j += "\"janMin\":" + String(intervaloJanela / 60000) + ",";
  j += "\"janAcom\":" + String(tempoAcomodacao / 1000);
  j += "},";

  j += "\"zonas\":[";
  for (int i = 0; i < 2; i++) {
    if (i) j += ",";
    unsigned long resta = 0;
    if (zonas[i].estado == IRRIG_ATIVA) {
      unsigned long dec = millis() - zonas[i].inicioIrrigacao;
      resta = (dec < zonas[i].duracaoAtual) ? (zonas[i].duracaoAtual - dec) / 1000 : 0;
    }
    long proxMin = -1;
    if (estrategia[i] == ESTR_AGENDADO && zonas[i].estado == IRRIG_OCIOSO) {
      unsigned long ref = zonas[i].inicioAgendamento;
      unsigned long dec = millis() - ref;
      proxMin = (dec >= intervaloAgendado[i]) ? 0
                : (long)((intervaloAgendado[i] - dec) / 60000UL);
    }
    j += "{\"estado\":\"" + nomeEstado(zonas[i].estado) + "\",";
    j += "\"estrategia\":\"" + String(estrategia[i]==ESTR_AGENDADO?"agendado":"limiar") + "\",";
    j += "\"confirmacoes\":" + String(zonas[i].confirmacoes) + ",";
    j += "\"restam\":" + String(resta) + ",";
    j += "\"proxMin\":" + String(proxMin) + ",";
    j += "\"total\":" + String(zonas[i].totalIrrigacoes) + ",";
    j += "\"bloqueado\":" + String(millis() < zonas[i].bloqueadoAte ? "true" : "false") + "}";
  }
  j += "],";

  j += "\"atuadores\":[";
  for (int i = 0; i < N_ATUADORES; i++) {
    Atuador& a = atuadores[i];
    if (i) j += ",";
    j += "{\"id\":\"" + String(a.id) + "\",\"nome\":\"" + String(a.nome) + "\",";
    j += "\"pino\":" + String(a.pino) + ",";
    j += "\"modo\":\"" + String(a.modo == AUTO ? "auto" : "manual") + "\",";
    j += "\"ligado\":" + String(a.ligado ? "true" : "false") + "}";
  }
  j += "]}";
  return j;
}

// Rota /cmd: modo e estado dos atuadores e intervalo do log.
void handleCmd() {
  if (estadoJanela != JAN_INATIVA) abortarJanela();

  if (server.hasArg("id")) {
    Atuador* a = buscarAtuador(server.arg("id"));
    if (a) {
      if (server.hasArg("modo"))
        setModo(*a, server.arg("modo") == "manual" ? MANUAL : AUTO);
      if (server.hasArg("estado"))
        setManual(*a, server.arg("estado") == "on");
    }
  }
  if (server.hasArg("intervaloLog"))
    setIntervaloLog((unsigned long)server.arg("intervaloLog").toInt() * 1000UL);
  aplicarAtuadores();
  server.send(200, "application/json", estadoJson());
}

// Rota /cfg: altera parametros pelo painel local.
void handleCfg() {
  if (server.hasArg("ledLiga"))    ledHoraLiga    = constrain(server.arg("ledLiga").toInt(), 0, 23);
  if (server.hasArg("ledDesliga")) ledHoraDesliga = constrain(server.arg("ledDesliga").toInt(), 0, 23);

  if (server.hasArg("aqLigaD")) aquecLigaDia      = server.arg("aqLigaD").toFloat();
  if (server.hasArg("aqDeslD")) aquecDesligaDia   = server.arg("aqDeslD").toFloat();
  if (server.hasArg("vtLigaD")) ventLigaDia       = server.arg("vtLigaD").toFloat();
  if (server.hasArg("vtDeslD")) ventDesligaDia    = server.arg("vtDeslD").toFloat();
  if (server.hasArg("exLigaD")) exausLigaDia      = server.arg("exLigaD").toFloat();
  if (server.hasArg("exDeslD")) exausDesligaDia   = server.arg("exDeslD").toFloat();

  if (server.hasArg("aqLigaN")) aquecLigaNoite    = server.arg("aqLigaN").toFloat();
  if (server.hasArg("aqDeslN")) aquecDesligaNoite = server.arg("aqDeslN").toFloat();
  if (server.hasArg("vtLigaN")) ventLigaNoite     = server.arg("vtLigaN").toFloat();
  if (server.hasArg("vtDeslN")) ventDesligaNoite  = server.arg("vtDeslN").toFloat();
  if (server.hasArg("exLigaN")) exausLigaNoite    = server.arg("exLigaN").toFloat();
  if (server.hasArg("exDeslN")) exausDesligaNoite = server.arg("exDeslN").toFloat();

  if (server.hasArg("estr1")) estrategia[0] = (server.arg("estr1") == "limiar") ? ESTR_LIMIAR : ESTR_AGENDADO;
  if (server.hasArg("estr2")) estrategia[1] = (server.arg("estr2") == "limiar") ? ESTR_LIMIAR : ESTR_AGENDADO;

  if (server.hasArg("vaz1"))  vazao[0]      = constrain(server.arg("vaz1").toInt(), 1, 1000);
  if (server.hasArg("vaz2"))  vazao[1]      = constrain(server.arg("vaz2").toInt(), 1, 1000);
  if (server.hasArg("dose1")) doseVolume[0] = constrain(server.arg("dose1").toInt(), 1, 5000);
  if (server.hasArg("dose2")) doseVolume[1] = constrain(server.arg("dose2").toInt(), 1, 5000);

  if (server.hasArg("agend1")) intervaloAgendado[0] = (unsigned long)constrain(server.arg("agend1").toInt(), 1, 720) * 3600000UL;
  if (server.hasArg("agend2")) intervaloAgendado[1] = (unsigned long)constrain(server.arg("agend2").toInt(), 1, 720) * 3600000UL;

  if (server.hasArg("lim1")) limiarIrrigar[0]  = constrain(server.arg("lim1").toInt(), 0, 4000);
  if (server.hasArg("lim2")) limiarIrrigar[1]  = constrain(server.arg("lim2").toInt(), 0, 4000);
  if (server.hasArg("sat1")) limiarSaturado[0] = constrain(server.arg("sat1").toInt(), 0, 4000);
  if (server.hasArg("sat2")) limiarSaturado[1] = constrain(server.arg("sat2").toInt(), 0, 4000);
  if (server.hasArg("superv")) supervisaoUmidade = (server.arg("superv").toInt() != 0);

  if (server.hasArg("kterm")) kTermico    = server.arg("kterm").toFloat();
  if (server.hasArg("tref"))  tempRefUmid = server.arg("tref").toFloat();

  if (server.hasArg("refU1")) refUmido[0] = constrain(server.arg("refU1").toInt(), 0, 4095);
  if (server.hasArg("refU2")) refUmido[1] = constrain(server.arg("refU2").toInt(), 0, 4095);
  if (server.hasArg("refS1")) refSeco[0]  = constrain(server.arg("refS1").toInt(), 0, 4095);
  if (server.hasArg("refS2")) refSeco[1]  = constrain(server.arg("refS2").toInt(), 0, 4095);

  if (server.hasArg("intMin"))
    intervaloMinimoIrrig = constrain(server.arg("intMin").toInt(), 5, 720) * 60000UL;
  if (server.hasArg("jCega"))
    janelaCega = constrain(server.arg("jCega").toInt(), 1, 180) * 60000UL;
  if (server.hasArg("conf"))
    confirmacoesNecessarias = constrain(server.arg("conf").toInt(), 1, 10);
  if (server.hasArg("janMin"))
    intervaloJanela = constrain(server.arg("janMin").toInt(), 1, 720) * 60000UL;
  if (server.hasArg("janAcom"))
    tempoAcomodacao = constrain(server.arg("janAcom").toInt(), 2, 120) * 1000UL;

  if (server.hasArg("circEn"))  circHabilitado     = (server.arg("circEn").toInt() != 0);
  if (server.hasArg("circIni")) circHoraIni        = constrain(server.arg("circIni").toInt(), 0, 23);
  if (server.hasArg("circFim")) circHoraFim        = constrain(server.arg("circFim").toInt(), 0, 24);
  if (server.hasArg("circOn"))  circLigaMs         = (unsigned long)(constrain(server.arg("circOn").toFloat(), 0.1, 60.0) * 60000.0);
  if (server.hasArg("circOff")) circDeslMs         = (unsigned long)(constrain(server.arg("circOff").toFloat(), 0.1, 240.0) * 60000.0);
  if (server.hasArg("circEx"))  circAcionaExaustor = (server.arg("circEx").toInt() != 0);

  registrarLinha('E', "config", "manual");
  salvarConfig();
  publicarConfigState();
  aplicarAtuadores();
  server.send(200, "application/json", estadoJson());
}

// Rota /estado: estado atual em JSON.
void handleEstado() { server.send(200, "application/json", estadoJson()); }

// Rota /medir: antecipa a janela de medicao.
void handleMedirAgora() {
  if (estadoJanela == JAN_INATIVA) ultimaJanela = millis() - intervaloJanela;
  server.send(200, "application/json", estadoJson());
}

// Rota /dose: dose manual por volume (ex.: /dose?id=valv1&ml=200).
void handleDose() {
  if (estadoJanela != JAN_INATIVA) abortarJanela();
  if (server.hasArg("id") && server.hasArg("ml")) {
    Atuador* a = buscarAtuador(server.arg("id"));
    int ml = server.arg("ml").toInt();
    if (a && ml > 0) {
      int iz = -1;
      for (int i = 0; i < 2; i++)
        if (&atuadores[zonas[i].indiceAtuador] == a) iz = i;
      if (iz >= 0 && vazao[iz] > 0 && zonas[iz].estado == IRRIG_OCIOSO) {
        unsigned long dur = (unsigned long)((double)ml / vazao[iz] * 60000.0);
        if (dur > TEMPO_MAX_VALVULA) dur = TEMPO_MAX_VALVULA;
        iniciarDose(iz, dur, "manual");
      }
    }
  }
  server.send(200, "application/json", estadoJson());
}

// Rota /log: download do CSV (todos os arquivos diarios em ordem, com um cabecalho so).
void handleLogDownload() {
  String arqs[MAX_ARQUIVOS_LOG];
  int n = listarArquivosLog(arqs, MAX_ARQUIVOS_LOG);
  if (n == 0) { server.send(404, "text/plain", "log inexistente"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=estufa_log.csv");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  char buf[1024];
  for (int i = 0; i < n; i++) {
    File f = LittleFS.open(arqs[i], "r");
    if (!f) continue;
    String cab = f.readStringUntil('\n');
    if (i == 0) server.sendContent(cab + "\n");
    while (f.available()) {
      size_t k = f.readBytes(buf, sizeof(buf));
      if (k == 0) break;
      server.sendContent(buf, k);
      esp_task_wdt_reset();
    }
    f.close();
  }
  server.sendContent("");
}

// Rota /log/registrar: grava uma linha manual.
void handleLogRegistrar() {
  registrarLinha('L', "manual", "manual");
  server.send(200, "application/json", estadoJson());
}

// Rota /log/limpar: apaga todo o log.
void handleLogLimpar() {
  limparTudo();
  server.send(200, "application/json", estadoJson());
}

// Rota /log/limpar_ate: apaga o log ate o epoch informado.
void handleLogLimparAte() {
  if (server.hasArg("epoch")) {
    unsigned long corte = strtoul(server.arg("epoch").c_str(), NULL, 10);
    limparAteEpoch(corte);
  }
  server.send(200, "application/json", estadoJson());
}

// Rota /: pagina do painel local.
void handleRoot() {
  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Estufa</title><style>"
    "body{font-family:sans-serif;max-width:470px;margin:auto;padding:20px}"
    ".card{border:1px solid #ddd;border-radius:10px;padding:16px;margin-top:14px}"
    ".t{font-size:42px;font-weight:bold}.on{color:#0a0;font-weight:bold}"
    ".off{color:#a00;font-weight:bold}"
    ".hora{font-size:24px;font-weight:bold;letter-spacing:1px}"
    "button{font-size:14px;padding:6px 11px;margin:3px;border-radius:8px;"
    "cursor:pointer;border:1px solid #bbb;background:#f5f5f5}"
    ".sel{background:#333;color:#fff;border-color:#333}"
    "a.btn{display:inline-block;text-decoration:none;color:#000}"
    "input{width:58px;font-size:14px}.u{font-size:26px;font-weight:bold}"
    ".at{border-top:1px solid #eee;padding:9px 0}.at:first-child{border-top:none}"
    ".zona{border-top:1px solid #eee;padding:8px 0}.zona:first-child{border-top:none}"
    ".nome{font-weight:bold}.obs{color:#888;font-size:13px}"
    ".alerta{background:#fff3cd;border:1px solid #e0c000;border-radius:8px;"
    "padding:10px;margin-top:14px;display:none}"
    "table.info td{padding:2px 8px 2px 0;font-size:14px}"
    "table.cfg td{padding:3px 6px 3px 0;font-size:13px}"
    "h4{margin:12px 0 4px 0}"
    "</style></head><body>"
    "<h2>Estufa - painel de controle</h2>"
    "<div class='alerta' id='alerta'></div>"

    "<div class='card'>Data / hora<div class='hora' id='hora'>--</div>"
    "<div class='obs' id='fonte'></div><div class='obs' id='rede'></div></div>"

    "<div class='card'>Temperatura<div class='t' id='temp'>--</div>"
    "<div class='obs' id='stt'></div><div class='obs' id='aqLim'></div></div>"

    "<div class='card'><b>Umidade - leitura de controle</b>"
    "<div style='margin-top:8px'>Zona 1<div class='u' id='k1'>--</div>"
    "<div class='obs' id='z1'></div></div>"
    "<div style='margin-top:10px'>Zona 2<div class='u' id='k2'>--</div>"
    "<div class='obs' id='z2'></div></div>"
    "<div class='obs' style='margin-top:10px' id='medInfo'></div>"
    "<div style='margin-top:8px'>"
    "<button onclick=\"fetch('/medir').then(r=>r.json()).then(render)\">Medir agora</button>"
    "</div></div>"

    "<div class='card'><b>Umidade - leituras cruas</b>"
    "<div style='margin-top:8px'>Bruta &nbsp; Z1: <b id='u1'>--</b> &nbsp; Z2: <b id='u2'>--</b></div>"
    "<div style='margin-top:4px'>Controle &nbsp; Z1: <b id='c1'>--</b> &nbsp; Z2: <b id='c2'>--</b></div>"
    "<div class='obs' style='margin-top:6px' id='desvio'></div>"
    "</div>"

    "<div class='card'><b>Acionamentos</b><div id='ats'></div></div>"

    "<div class='card'><b>Parametros termicos</b>"
    "<table class='cfg'>"
    "<tr><td></td><td><b>Liga</b></td><td><b>Desliga</b></td></tr>"
    "<tr><td>Aquecedor dia</td><td><input id='aqLigaD' type='number' step='0.5'></td>"
    "<td><input id='aqDeslD' type='number' step='0.5'></td></tr>"
    "<tr><td>Ventilador dia</td><td><input id='vtLigaD' type='number' step='0.5'></td>"
    "<td><input id='vtDeslD' type='number' step='0.5'></td></tr>"
    "<tr><td>Exaustor dia</td><td><input id='exLigaD' type='number' step='0.5'></td>"
    "<td><input id='exDeslD' type='number' step='0.5'></td></tr>"
    "<tr><td>Aquecedor noite</td><td><input id='aqLigaN' type='number' step='0.5'></td>"
    "<td><input id='aqDeslN' type='number' step='0.5'></td></tr>"
    "<tr><td>Ventilador noite</td><td><input id='vtLigaN' type='number' step='0.5'></td>"
    "<td><input id='vtDeslN' type='number' step='0.5'></td></tr>"
    "<tr><td>Exaustor noite</td><td><input id='exLigaN' type='number' step='0.5'></td>"
    "<td><input id='exDeslN' type='number' step='0.5'></td></tr>"
    "</table>"
    "<h4>Fotoperiodo</h4>"
    "LED liga <input id='ledLiga' type='number' min='0' max='23'> h, "
    "desliga <input id='ledDesliga' type='number' min='0' max='23'> h"
    "<div style='margin-top:8px'><button onclick='salvarCfg()'>Salvar parametros</button></div>"
    "</div>";
  html +=
    "<div class='card'><b>Irrigacao por quantidade</b>"

    "<div class='zona'><div class='nome'>Zona 1 <span class='obs' id='es1'></span></div>"
    "<div style='margin:4px 0'>"
    "<button id='ea1' onclick=\"cfg('estr1=agendado')\">Agendado</button>"
    "<button id='el1' onclick=\"cfg('estr1=limiar')\">Limiar</button></div>"
    "Volume <input id='dose1' type='number'> mL &nbsp; Vazao <input id='vaz1' type='number'> mL/min<br>"
    "A cada <input id='agend1' type='number'> h &nbsp;&nbsp; Limiar <input id='lim1' type='number'> ADC "
    "&nbsp; Saturado <input id='sat1' type='number'> ADC"
    "<div style='margin-top:6px'>Dose manual <input id='dm1' type='number' value='200'> mL "
    "<button onclick=\"dose('valv1','dm1')\">Dosar agora</button></div></div>"

    "<div class='zona'><div class='nome'>Zona 2 <span class='obs' id='es2'></span></div>"
    "<div style='margin:4px 0'>"
    "<button id='ea2' onclick=\"cfg('estr2=agendado')\">Agendado</button>"
    "<button id='el2' onclick=\"cfg('estr2=limiar')\">Limiar</button></div>"
    "Volume <input id='dose2' type='number'> mL &nbsp; Vazao <input id='vaz2' type='number'> mL/min<br>"
    "A cada <input id='agend2' type='number'> h &nbsp;&nbsp; Limiar <input id='lim2' type='number'> ADC "
    "&nbsp; Saturado <input id='sat2' type='number'> ADC"
    "<div style='margin-top:6px'>Dose manual <input id='dm2' type='number' value='200'> mL "
    "<button onclick=\"dose('valv2','dm2')\">Dosar agora</button></div></div>"

    "<div style='margin-top:8px'>Supervisao por umidade "
    "<button id='sv1' onclick=\"cfg('superv=1')\">On</button>"
    "<button id='sv0' onclick=\"cfg('superv=0')\">Off</button> "
    "</div>"
    "<div style='margin-top:6px'>Confirmacoes <input id='conf' type='number'> "
    "</div>"
    "<div style='margin-top:6px'>Intervalo minimo <input id='intMin' type='number'> min "
    "&nbsp; Janela cega <input id='jCega' type='number'> min</div>"
    "<div style='margin-top:6px'>Janela de medicao a cada <input id='janMin' type='number'> min, "
    "acomodacao <input id='janAcom' type='number'> s</div>"
    "<div style='margin-top:8px'><button onclick='salvarIrrig()'>Salvar irrigacao</button></div>"
    "</div>"

    "<div class='card'><b>Compensacao termica e calibracao</b>"
    "<div style='margin-top:8px'>k <input id='kterm' type='number' step='0.1'> ADC/C "
    "&nbsp; Tref <input id='tref' type='number' step='0.5'> C</div>"
    "<table class='cfg' style='margin-top:6px'>"
    "<tr><td></td><td><b>Umido (0%)</b></td><td><b>Seco (100%)</b></td></tr>"
    "<tr><td>Zona 1</td><td><input id='refU1' type='number'></td><td><input id='refS1' type='number'></td></tr>"
    "<tr><td>Zona 2</td><td><input id='refU2' type='number'></td><td><input id='refS2' type='number'></td></tr>"
    "</table>"
    "<div style='margin-top:8px'><button onclick='salvarCalib()'>Salvar calibracao</button></div>"
    "</div>"

    "<div class='card'><b>Circulacao (polinizacao)</b>"
    "<table class='info'>"
    "<tr><td>Ativo</td><td>"
    "<button id='cE1' onclick=\"cfg('circEn=1')\">SIM</button>"
    "<button id='cE0' onclick=\"cfg('circEn=0')\">NAO</button></td></tr>"
    "<tr><td>Janela (h)</td><td><input id='circIni' type='number' min='0' max='23' style='width:60px'> as "
    "<input id='circFim' type='number' min='0' max='24' style='width:60px'></td></tr>"
    "<tr><td>Ligado (min)</td><td><input id='circOn' type='number' min='0.1' step='0.1' style='width:70px'></td></tr>"
    "<tr><td>Desligado (min)</td><td><input id='circOff' type='number' min='0.1' step='0.1' style='width:70px'></td></tr>"
    "<tr><td>Aciona exaustor junto</td><td>"
    "<button id='cX1' onclick=\"cfg('circEx=1')\">SIM</button>"
    "<button id='cX0' onclick=\"cfg('circEx=0')\">NAO</button></td></tr>"
    "</table>"
    "<div style='margin-top:8px'><button onclick='salvarCirc()'>Salvar circulacao</button></div>"
    "</div>"

    "<div class='card'><b>Registro local</b>"
    "<table class='info'>"
    "<tr><td>Registros</td><td id='lgLin'>--</td></tr>"
    "<tr><td>Arquivos</td><td id='lgArq'>--</td></tr>"
    "<tr><td>Tamanho</td><td id='lgTam'>--</td></tr>"
    "<tr><td>Particao</td><td id='lgPart'>--</td></tr>"
    "<tr><td>Ultimo</td><td id='lgUlt'>--</td></tr>"
    "<tr><td>Descartados</td><td id='lgDesc'>--</td></tr>"
    "<tr><td>Falhas de gravacao</td><td id='lgFal'>--</td></tr>"
    "</table>"
    "<div style='margin-top:10px'>Intervalo: <input id='iLog' type='number' min='5' max='3600'> s "
    "<button onclick=\"cmd('intervaloLog=10')\">10 s</button>"
    "<button onclick=\"cmd('intervaloLog=300')\">300 s</button></div>"
    "<div style='margin-top:10px'>"
    "<a class='btn' href='/log' target='_blank'><button>Baixar CSV</button></a>"
    "<button onclick='baixarApagar()'>Baixar e apagar</button>"
    "<button onclick=\"fetch('/log/registrar').then(r=>r.json()).then(render)\">Registrar</button>"
    "<button onclick='limpar()'>Limpar tudo</button></div>"
    "</div>";
  html +=
    "<script>"
    "var montado=false;"
    "function cmd(q){fetch('/cmd?'+q).then(r=>r.json()).then(render);}"
    "function cfg(q){fetch('/cfg?'+q).then(r=>r.json()).then(render);}"
    "function dose(id,inp){fetch('/dose?id='+id+'&ml='+g(inp)).then(r=>r.json()).then(render);}"
    "function limpar(){if(confirm('Apagar TODOS os registros?'))"
    "fetch('/log/limpar').then(r=>r.json()).then(render);}"
    "function baixarApagar(){"
    "fetch('/estado').then(r=>r.json()).then(function(d){var ep=d.ultimoEpoch;"
    "fetch('/log').then(function(r){if(!r.ok)throw new Error('download falhou');return r.blob();})"
    ".then(function(b){var url=URL.createObjectURL(b);var a=document.createElement('a');"
    "a.href=url;a.download='estufa_log.csv';document.body.appendChild(a);a.click();"
    "document.body.removeChild(a);URL.revokeObjectURL(url);"
    "return fetch('/log/limpar_ate?epoch='+ep);}).then(r=>r.json()).then(render)"
    ".catch(function(e){alert('Baixar e apagar: '+e.message+' - nada foi apagado.');});});}"
    "function kb(b){return (b/1024).toFixed(1)+' KB';}"
    "function txt(id,v){var e=document.getElementById(id);if(e&&e.innerText!=v)e.innerText=v;}"
    "function val(id,v){var e=document.getElementById(id);"
    "if(e&&document.activeElement.id!=id)e.value=v;}"
    "function g(id){return document.getElementById(id).value;}"
    "function salvarCfg(){cfg('aqLigaD='+g('aqLigaD')+'&aqDeslD='+g('aqDeslD')"
    "+'&vtLigaD='+g('vtLigaD')+'&vtDeslD='+g('vtDeslD')"
    "+'&exLigaD='+g('exLigaD')+'&exDeslD='+g('exDeslD')"
    "+'&aqLigaN='+g('aqLigaN')+'&aqDeslN='+g('aqDeslN')"
    "+'&vtLigaN='+g('vtLigaN')+'&vtDeslN='+g('vtDeslN')"
    "+'&exLigaN='+g('exLigaN')+'&exDeslN='+g('exDeslN')"
    "+'&ledLiga='+g('ledLiga')+'&ledDesliga='+g('ledDesliga'));}"
    "function salvarIrrig(){cfg('dose1='+g('dose1')+'&dose2='+g('dose2')"
    "+'&vaz1='+g('vaz1')+'&vaz2='+g('vaz2')"
    "+'&agend1='+g('agend1')+'&agend2='+g('agend2')"
    "+'&lim1='+g('lim1')+'&lim2='+g('lim2')"
    "+'&sat1='+g('sat1')+'&sat2='+g('sat2')"
    "+'&conf='+g('conf')+'&intMin='+g('intMin')+'&jCega='+g('jCega')"
    "+'&janMin='+g('janMin')+'&janAcom='+g('janAcom'));}"
    "function salvarCalib(){cfg('kterm='+g('kterm')+'&tref='+g('tref')"
    "+'&refU1='+g('refU1')+'&refU2='+g('refU2')"
    "+'&refS1='+g('refS1')+'&refS2='+g('refS2'));}"
    "function salvarCirc(){cfg('circIni='+g('circIni')+'&circFim='+g('circFim')"
    "+'&circOn='+g('circOn')+'&circOff='+g('circOff'));}"
    "function montar(d){var h='';"
    "d.atuadores.forEach(function(a){"
    "h+=\"<div class='at'><div class='nome'>\"+a.nome+\" <span class='obs'>(GPIO \"+a.pino+\")</span></div>\";"
    "h+=\"<div style='margin:4px 0'>Estado: <span id='st_\"+a.id+\"'></span></div>\";"
    "h+=\"<button id='ba_\"+a.id+\"' onclick=\\\"cmd('id=\"+a.id+\"&modo=auto')\\\">AUTO</button>\";"
    "h+=\"<button id='bm_\"+a.id+\"' onclick=\\\"cmd('id=\"+a.id+\"&modo=manual')\\\">MANUAL</button>\";"
    "h+=\"<span id='mc_\"+a.id+\"'> &nbsp; \";"
    "h+=\"<button onclick=\\\"cmd('id=\"+a.id+\"&estado=on')\\\">Ligar</button>\";"
    "h+=\"<button onclick=\\\"cmd('id=\"+a.id+\"&estado=off')\\\">Desligar</button></span></div>\";"
    "});document.getElementById('ats').innerHTML=h;montado=true;}"
    "function render(d){if(!montado)montar(d);"
    "txt('hora',d.hora);"
    "txt('fonte','Fonte: '+d.fonteHora+'  |  periodo: '+d.periodo+'  |  firmware v'+d.fw);"
    "txt('rede',(d.wifi?'WiFi - '+d.ip:'WiFi desconectado')+'  |  quedas: '+d.quedasWifi"
    "+'  |  MQTT: '+(d.mqtt?'on':'off')+' ('+d.mqttPub+')');"
    "txt('temp',(d.temp==null?'--':d.temp.toFixed(2)+' C'));"
    "txt('stt',d.status);"
    "txt('aqLim',d.aquecLimite?'Aquecedor no limite termico':'');"
    "txt('u1',d.umid1);txt('u2',d.umid2);"
    "txt('c1',d.ctrl1<0?'--':String(d.ctrl1));txt('c2',d.ctrl2<0?'--':String(d.ctrl2));"
    "txt('k1',d.corr1<0?'--':String(d.corr1));txt('k2',d.corr2<0?'--':String(d.corr2));"
    "txt('medInfo',(d.janelaAtiva?'MEDINDO AGORA':"
    "'Ultima: '+d.ultimaMedicao+'  |  proxima em '+d.proximaMedicao+' min')"
    "+'  |  total: '+d.totalMedicoes);"
    "txt('desvio',(d.ctrl1<0)?'':('Offset atual: zona 1 '+(d.umid1-d.ctrl1)"
    "+' , zona 2 '+(d.umid2-d.ctrl2)+' pontos'));"
    "for(var i=0;i<2;i++){var z=d.zonas[i];"
    "var s=z.estrategia+' | '+z.estado;"
    "if(z.estrategia=='limiar')s+=' '+z.confirmacoes+'/'+d.cfg.conf+' conf.';"
    "if(z.estrategia=='agendado'&&z.proxMin>=0)s+=' | proxima em '+z.proxMin+' min';"
    "s+=' | irrigacoes: '+z.total;"
    "if(z.restam>0)s+=' | restam '+z.restam+' s';"
    "var pct=(i==0?d.pct1:d.pct2);var raw=(i==0?d.ctrl1:d.ctrl2);"
    "if(pct>=0)s+=' | '+pct+'% depl.';"
    "s+=' | cru '+raw;"
    "if(z.bloqueado)s+=' | BLOQUEADA (salto)';"
    "txt('z'+(i+1),s);}"
    "txt('es1',d.cfg.estr1);txt('es2',d.cfg.estr2);"
    "var al=document.getElementById('alerta');"
    "al.style.display=d.alerta?'block':'none';if(d.alerta)txt('alerta',d.alerta);"
    "txt('lgLin',String(d.logLinhas));txt('lgTam',kb(d.logBytes)+(d.logCheio?'  (CHEIO)':''));"
    "txt('lgPart',kb(d.fsUsado)+' / '+kb(d.fsTotal));"
    "txt('lgUlt',d.ultimoRegistro);txt('lgDesc',String(d.descartados));"
    "txt('lgArq',d.logArquivos);txt('lgFal',String(d.falhasLog));"
    "d.atuadores.forEach(function(a){"
    "var s=document.getElementById('st_'+a.id);var v=a.ligado?'LIGADO':'DESLIGADO';"
    "if(s.innerText!=v){s.innerText=v;s.className=a.ligado?'on':'off';}"
    "document.getElementById('ba_'+a.id).className=(a.modo=='auto')?'sel':'';"
    "document.getElementById('bm_'+a.id).className=(a.modo=='manual')?'sel':'';"
    "document.getElementById('mc_'+a.id).style.visibility=(a.modo=='manual')?'visible':'hidden';"
    "});"
    "var c=d.cfg;"
    "document.getElementById('ea1').className=(c.estr1=='agendado')?'sel':'';"
    "document.getElementById('el1').className=(c.estr1=='limiar')?'sel':'';"
    "document.getElementById('ea2').className=(c.estr2=='agendado')?'sel':'';"
    "document.getElementById('el2').className=(c.estr2=='limiar')?'sel':'';"
    "document.getElementById('sv1').className=(c.superv==1)?'sel':'';"
    "document.getElementById('sv0').className=(c.superv==0)?'sel':'';"
    "val('aqLigaD',c.aqLigaD);val('aqDeslD',c.aqDeslD);"
    "val('vtLigaD',c.vtLigaD);val('vtDeslD',c.vtDeslD);"
    "val('exLigaD',c.exLigaD);val('exDeslD',c.exDeslD);"
    "val('aqLigaN',c.aqLigaN);val('aqDeslN',c.aqDeslN);"
    "val('vtLigaN',c.vtLigaN);val('vtDeslN',c.vtDeslN);"
    "val('exLigaN',c.exLigaN);val('exDeslN',c.exDeslN);"
    "val('ledLiga',c.ledLiga);val('ledDesliga',c.ledDesliga);"
    "val('dose1',c.dose1);val('dose2',c.dose2);val('vaz1',c.vaz1);val('vaz2',c.vaz2);"
    "val('agend1',c.agend1);val('agend2',c.agend2);"
    "val('lim1',c.lim1);val('lim2',c.lim2);val('sat1',c.sat1);val('sat2',c.sat2);"
    "val('conf',c.conf);val('intMin',c.intMin);val('jCega',c.jCega);"
    "val('janMin',c.janMin);val('janAcom',c.janAcom);"
    "val('kterm',c.kterm);val('tref',c.tref);"
    "val('refU1',c.refU1);val('refU2',c.refU2);val('refS1',c.refS1);val('refS2',c.refS2);"
    "document.getElementById('cE1').className=(c.circEn==1)?'sel':'';"
    "document.getElementById('cE0').className=(c.circEn==0)?'sel':'';"
    "document.getElementById('cX1').className=(c.circEx==1)?'sel':'';"
    "document.getElementById('cX0').className=(c.circEx==0)?'sel':'';"
    "val('circIni',c.circIni);val('circFim',c.circFim);val('circOn',c.circOn);val('circOff',c.circOff);"
    "val('iLog',d.intervaloLog);}"
    "function poll(){fetch('/estado').then(r=>r.json()).then(render);}"
    "poll();setInterval(poll,2000);"
    "</script></body></html>";
  server.send(200, "text/html", html);
}
