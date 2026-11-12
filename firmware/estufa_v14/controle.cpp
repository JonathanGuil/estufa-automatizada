// Logica de controle: decisoes automaticas, circulacao e supervisao do aquecedor.

#include <Arduino.h>
#include <math.h>
#include "controle.h"
#include "atuadores.h"
#include "parametros.h"
#include "hora.h"
#include "sensores.h"
#include "armazenamento.h"
#include "irrigacao.h"

// Estado do ciclo de circulacao
bool          circPulsoAtivo      = false;
unsigned long circUltimaTransicao = 0;

// Supervisao do aquecedor
const unsigned long JANELA_VERIF_AQUEC = 600000UL;
const float DELTA_MIN_AQUEC   = 0.30;
const float DELTA_QUEDA_AQUEC = 0.30;
unsigned long aquecLigadoDesde = 0;
float aquecTempInicial = NAN;
bool  alertaAquecedor = false;
bool  aquecNoLimite   = false;
String textoAlerta = "";   // alerta exibido no painel local

// Guarda o instante e a temperatura em que o aquecedor ligou, para detectar falha.
void acompanharAquecedor(bool ligou) {
  if (ligou) {
    aquecLigadoDesde = millis(); aquecTempInicial = ultimaTempValida;
    alertaAquecedor = false; aquecNoLimite = false; textoAlerta = "";
  } else { aquecLigadoDesde = 0; aquecTempInicial = NAN; aquecNoLimite = false; }
}

// Detecta falha do aquecedor (temperatura caindo com ele ligado) e aquecedor no limite termico.
void verificarAquecedor() {
  if (!atuadores[AT_AQUEC].ligado || aquecLigadoDesde == 0) { aquecNoLimite = false; return; }
  if (sensorPerdido || isnan(aquecTempInicial)) return;
  if (millis() - aquecLigadoDesde < JANELA_VERIF_AQUEC) return;

  float delta = ultimaTempValida - aquecTempInicial;

  if (delta <= -DELTA_QUEDA_AQUEC) {
    aquecNoLimite = false;
    if (!alertaAquecedor) {
      alertaAquecedor = true;
      textoAlerta = "Temperatura caiu " + String(delta, 2) + " C em " +
                    String(JANELA_VERIF_AQUEC / 60000) + " min com o aquecedor ligado - "
                    "verificar rele, soquete ou lampada.";
      registrarLinha('E', "falha_aquec", "seguranca");
    }
  } else if (delta < DELTA_MIN_AQUEC) {
    aquecNoLimite = true;
    alertaAquecedor = false; textoAlerta = "";
  } else {
    aquecNoLimite = false;
    alertaAquecedor = false; textoAlerta = "";
  }

  aquecLigadoDesde = millis();
  aquecTempInicial = ultimaTempValida;
}

// Decisao automatica do LED pelo fotoperiodo.
bool autoLed() {
  if (!horaValida) return atuadores[AT_LED].ligado;
  return ehDia();
}

// Decisao automatica do ventilador pela temperatura (com histerese).
bool autoVentilador() {
  if (sensorPerdido) return false;
  float liga    = ehDia() ? ventLigaDia    : ventLigaNoite;
  float desliga = ehDia() ? ventDesligaDia : ventDesligaNoite;
  if (ultimaTempValida >= liga)    return true;
  if (ultimaTempValida <= desliga) return false;
  return atuadores[AT_VENT].ligado;
}

// Decisao automatica do exaustor pela temperatura (com histerese).
bool autoExaustor() {
  if (sensorPerdido) return false;
  float liga    = ehDia() ? exausLigaDia    : exausLigaNoite;
  float desliga = ehDia() ? exausDesligaDia : exausDesligaNoite;
  if (ultimaTempValida >= liga)    return true;
  if (ultimaTempValida <= desliga) return false;
  return atuadores[AT_EXAUS].ligado;
}

// Avanca o ciclo de circulacao e indica se o pulso esta ativo agora. Chamar uma vez por ciclo.
bool circulacaoAtiva() {
  if (!circHabilitado) { circPulsoAtivo = false; return false; }
  int h = horaAtual();
  if (h < 0) { circPulsoAtivo = false; return false; }
  bool naJanela = (circHoraIni < circHoraFim)
                    ? (h >= circHoraIni && h < circHoraFim)
                    : (h >= circHoraIni || h < circHoraFim);
  if (!naJanela) { circPulsoAtivo = false; return false; }
  unsigned long agora = millis();
  unsigned long dur = circPulsoAtivo ? circLigaMs : circDeslMs;
  if (agora - circUltimaTransicao >= dur) {
    circPulsoAtivo = !circPulsoAtivo;
    circUltimaTransicao = agora;
  }
  return circPulsoAtivo;
}

// Decisao automatica do aquecedor pela temperatura (desligado se ventilador ou exaustor estiverem ligados).
bool autoAquecedor() {
  if (sensorPerdido) return false;
  if (atuadores[AT_VENT].ligado || atuadores[AT_EXAUS].ligado) return false;
  float liga    = ehDia() ? aquecLigaDia    : aquecLigaNoite;
  float desliga = ehDia() ? aquecDesligaDia : aquecDesligaNoite;
  if (ultimaTempValida <= liga)    return true;
  if (ultimaTempValida >= desliga) return false;
  return atuadores[AT_AQUEC].ligado;
}

// Aplica as decisoes automaticas ou manuais a todos os atuadores.
void aplicarAtuadores() {
  Atuador& vent  = atuadores[AT_VENT];
  Atuador& exaus = atuadores[AT_EXAUS];
  Atuador& aquec = atuadores[AT_AQUEC];
  Atuador& led   = atuadores[AT_LED];

  bool circ = circulacaoAtiva();
  bool alvoVent  = vent.modo  == MANUAL ? vent.manualLigado  : (autoVentilador() || circ);
  bool alvoExaus = exaus.modo == MANUAL ? exaus.manualLigado : (autoExaustor() || (circ && circAcionaExaustor));
  escrever(vent,  alvoVent,  vent.modo  == MANUAL ? "manual" : "logica");
  escrever(exaus, alvoExaus, exaus.modo == MANUAL ? "manual" : "logica");
  escrever(aquec, aquec.modo == MANUAL ? aquec.manualLigado : autoAquecedor(),
           aquec.modo == MANUAL ? "manual" : "logica");
  escrever(led,   led.modo   == MANUAL ? led.manualLigado   : autoLed(),
           led.modo   == MANUAL ? "manual" : "logica");

  for (int iz = 0; iz < 2; iz++) {
    Atuador& v = atuadores[zonas[iz].indiceAtuador];
    if (v.modo == MANUAL) {
      if (zonas[iz].estado == IRRIG_ATIVA) continue;
      bool antes = v.ligado;
      escrever(v, v.manualLigado, "manual");
      if (!antes && v.ligado) {
        zonas[iz].inicioIrrigacao = millis();
        if (doseManualReiniciaIntervalo) marcarAgendamento(iz);
      }
    }
  }

  protegerValvulas();
}
