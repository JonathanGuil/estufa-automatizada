// Irrigacao por zona: doses por volume, agendamento, limiar e protecao das valvulas.

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
#include "irrigacao.h"
#include "atuadores.h"
#include "parametros.h"
#include "janela.h"
#include "armazenamento.h"
#include "controle.h"
#include "hora.h"
#include "mqtt.h"

ZonaIrrig zonas[2] = {
  { AT_VALV1, IRRIG_OCIOSO, 0, 0, 0, 0, 0, -1, 0, "sistema", 0 },
  { AT_VALV2, IRRIG_OCIOSO, 0, 0, 0, 0, 0, -1, 0, "sistema", 0 }
};

unsigned long ultimoTickIrrig = 0;

// Estado da irrigacao gravado no LittleFS para sobreviver a reinicios
struct DosePersistida {
  uint32_t      refEpoch;        // referencia do agendamento
  uint32_t      fimEpoch;        // fim da ultima dose
  bool          ativa;           // dose em curso
  uint32_t      iniEpoch;        // inicio da dose em curso
  unsigned long duracaoTotal;    // ms da dose completa
  unsigned long entregue;        // ms ja entregues, somando as tentativas
  unsigned long entregueAntes;   // ms entregues antes da tentativa atual
  uint8_t       tentativas;      // retomadas feitas
  bool          retomar;         // retomar assim que possivel
  char          origem[12];
};

DosePersistida persist[2];

const char* ARQ_IRRIG = "/irrig.txt";
const unsigned long INTERVALO_CHECKPOINT = 5000UL;   // ms entre gravacoes do tempo entregue
const unsigned long RESTANTE_MINIMO = 2000UL;        // sobra menor que isto nao e retomada
const uint8_t MAX_RETOMADAS = 2;
unsigned long ultimoCheckpoint[2] = { 0, 0 };

// Grava o estado da irrigacao (so com hora valida, para os epochs fazerem sentido).
void salvarIrrigacao() {
  if (!fsPronto || !horaValida) return;
  File f = LittleFS.open(ARQ_IRRIG, "w");
  if (!f) return;
  for (int iz = 0; iz < 2; iz++) {
    DosePersistida& p = persist[iz];
    f.printf("%lu,%lu,%d,%lu,%lu,%lu,%u,%s\n",
             (unsigned long)p.refEpoch, (unsigned long)p.fimEpoch, p.ativa ? 1 : 0,
             (unsigned long)p.iniEpoch, p.duracaoTotal, p.entregue,
             (unsigned)p.tentativas, p.origem);
  }
  f.close();
}

// Le o estado gravado; retorna false se nao existir ou estiver incompleto.
bool carregarArquivoIrrigacao() {
  if (!fsPronto || !LittleFS.exists(ARQ_IRRIG)) return false;
  File f = LittleFS.open(ARQ_IRRIG, "r");
  if (!f) return false;
  DosePersistida lido[2];
  memset(lido, 0, sizeof(lido));
  bool ok = true;
  for (int iz = 0; iz < 2 && ok; iz++) {
    String l = f.readStringUntil('\n');
    l.trim();
    String c[8];
    if (dividirCsv(l, c, 8) < 8) { ok = false; break; }
    lido[iz].refEpoch     = strtoul(c[0].c_str(), nullptr, 10);
    lido[iz].fimEpoch     = strtoul(c[1].c_str(), nullptr, 10);
    lido[iz].ativa        = c[2] == "1";
    lido[iz].iniEpoch     = strtoul(c[3].c_str(), nullptr, 10);
    lido[iz].duracaoTotal = strtoul(c[4].c_str(), nullptr, 10);
    lido[iz].entregue     = strtoul(c[5].c_str(), nullptr, 10);
    lido[iz].tentativas   = (uint8_t)c[6].toInt();
    strncpy(lido[iz].origem, c[7].c_str(), sizeof(lido[iz].origem) - 1);
  }
  f.close();
  if (ok) memcpy(persist, lido, sizeof(persist));
  return ok;
}

// Sem estado gravado: usa a ultima dose registrada no log como referencia do agendamento.
void importarUltimaDoseDoLog() {
  String arqs[MAX_ARQUIVOS_LOG];
  int n = listarArquivosLog(arqs, MAX_ARQUIVOS_LOG);
  for (int i = 0; i < n; i++) {
    File f = LittleFS.open(arqs[i], "r");
    if (!f) continue;
    while (f.available()) {
      String l = f.readStringUntil('\n');
      if (l.length() < 10 || l.charAt(0) != 'E' || l.indexOf(",valv") < 0) continue;
      l.trim();
      String c[24];
      if (dividirCsv(l, c, 24) < 24) continue;
      int iz = (c[22] == "valv1") ? 0 : (c[22] == "valv2") ? 1 : -1;
      if (iz < 0) continue;
      uint32_t ep = strtoul(c[1].c_str(), nullptr, 10);
      bool abriu = c[14 + iz] == "1";
      bool reinicia = c[23] == "logica" || (doseManualReiniciaIntervalo && c[23] == "manual");
      if (abriu && reinicia) persist[iz].refEpoch = ep;
      if (!abriu) persist[iz].fimEpoch = ep;
    }
    f.close();
  }
  for (int iz = 0; iz < 2; iz++)
    if (persist[iz].refEpoch)
      Serial.printf("Zona %d: ultima dose do log em %lu\n", iz + 1, (unsigned long)persist[iz].refEpoch);
}

// Converte um epoch passado no valor de millis() equivalente (aritmetica sem sinal).
unsigned long millisDoEpoch(uint32_t ep, uint32_t agora) {
  uint32_t dec = agora - ep;
  if (dec > 4000000UL) dec = 4000000UL;   // limita a ~46 dias (millis estoura em ~49)
  return millis() - dec * 1000UL;
}

// Detalhe dos eventos de retomada: zona e segundos que faltavam.
String extraDose(int iz, unsigned long faltaMs) {
  return "\"zona\":" + String(iz + 1) + ",\"falta_s\":" + String(faltaMs / 1000UL);
}

// No boot: recupera a contagem do agendamento e prepara a retomada da dose interrompida.
void restaurarIrrigacao() {
  bool temArquivo = carregarArquivoIrrigacao();
  if (!horaValida) {
    zonas[0].inicioAgendamento = millis();
    zonas[1].inicioAgendamento = millis();
    return;
  }
  if (!temArquivo) importarUltimaDoseDoLog();

  uint32_t agora = (uint32_t)time(nullptr);
  for (int iz = 0; iz < 2; iz++) {
    DosePersistida& p = persist[iz];
    ZonaIrrig& z = zonas[iz];

    if (p.refEpoch > 0 && p.refEpoch <= agora) z.inicioAgendamento = millisDoEpoch(p.refEpoch, agora);
    else z.inicioAgendamento = millis();

    if (p.fimEpoch > 0 && p.fimEpoch <= agora) {
      z.fimIrrigacao = millisDoEpoch(p.fimEpoch, agora);
      if (z.fimIrrigacao == 0) z.fimIrrigacao = 1;
      if (agora - p.fimEpoch < janelaCega / 1000UL) z.estado = IRRIG_ESPERA;
    }

    if (p.ativa) {
      unsigned long falta = (p.duracaoTotal > p.entregue) ? p.duracaoTotal - p.entregue : 0;
      if (falta >= RESTANTE_MINIMO && p.tentativas < MAX_RETOMADAS) {
        p.tentativas++;
        p.retomar = true;
        z.estado = IRRIG_OCIOSO;
      } else {
        p.ativa = false;
        p.fimEpoch = agora;
        if (falta >= RESTANTE_MINIMO)
          registrarLinha('E', "dose_abandonada", "seguranca", extraDose(iz, falta));
      }
    }
  }
  salvarIrrigacao();
}

// Indica se ha dose interrompida esperando para ser retomada.
bool haDoseParaRetomar() { return persist[0].retomar || persist[1].retomar; }

// Reinicia a contagem do agendamento da zona e grava a referencia.
void marcarAgendamento(int iz) {
  zonas[iz].inicioAgendamento = millis();
  persist[iz].refEpoch = horaValida ? (uint32_t)time(nullptr) : 0;
  salvarIrrigacao();
}

// Marca a dose da zona como terminada (no tempo, por seguranca ou por troca de modo).
void encerrarDose(int iz) {
  DosePersistida& p = persist[iz];
  if (!p.ativa && !p.retomar) return;
  p.ativa = false;
  p.retomar = false;
  p.fimEpoch = horaValida ? (uint32_t)time(nullptr) : 0;
  salvarIrrigacao();
}

// Tempo de valvula aberta (ms) para entregar a dose da zona na vazao medida.
unsigned long duracaoDose(int iz) {
  if (vazao[iz] <= 0) return 0;
  return (unsigned long)((double)doseVolume[iz] / vazao[iz] * 60000.0);
}

// Indica se o solo da zona ja esta umido pela supervisao de umidade.
bool solosaturado(int iz) {
  int c = (iz == 0) ? umidadeControleCorr1 : umidadeControleCorr2;
  if (c < 0) return false;
  return c < limiarSaturado[iz];
}

// Indica se a valvula da outra zona esta aberta (as doses automaticas sao uma de cada vez).
bool outraValvulaAberta(int iz) {
  const ZonaIrrig& o = zonas[1 - iz];
  return o.estado == IRRIG_ATIVA || atuadores[o.indiceAtuador].ligado;
}

// Abre a valvula da zona pelo tempo informado e registra a origem da dose.
void iniciarDose(int iz, unsigned long duracao, const char* origem) {
  if (duracao == 0) return;
  ZonaIrrig& z = zonas[iz];
  Atuador& v = atuadores[z.indiceAtuador];
  DosePersistida& p = persist[iz];
  z.estado = IRRIG_ATIVA;
  z.inicioIrrigacao = millis();
  z.duracaoAtual = duracao;
  z.confirmacoes = 0;
  z.totalIrrigacoes++;
  z.origemDose = origem;

  p.ativa = true;
  p.retomar = false;
  p.iniEpoch = horaValida ? (uint32_t)time(nullptr) : 0;
  p.duracaoTotal = duracao;
  p.entregue = 0;
  p.entregueAntes = 0;
  p.tentativas = 0;
  strncpy(p.origem, origem, sizeof(p.origem) - 1);
  p.origem[sizeof(p.origem) - 1] = '\0';
  ultimoCheckpoint[iz] = millis();

  if (strcmp(origem, "manual") != 0 || doseManualReiniciaIntervalo) {
    z.inicioAgendamento = millis();
    p.refEpoch = p.iniEpoch;
  }
  salvarIrrigacao();
  escrever(v, true, origem);
}

// Reabre a valvula pelo tempo que faltou da dose interrompida (a referencia do agendamento nao muda).
void retomarDose(int iz) {
  ZonaIrrig& z = zonas[iz];
  DosePersistida& p = persist[iz];
  p.retomar = false;
  unsigned long falta = (p.duracaoTotal > p.entregue) ? p.duracaoTotal - p.entregue : 0;
  if (falta < RESTANTE_MINIMO) { encerrarDose(iz); return; }

  registrarLinha('E', "dose_retomada", "sistema", extraDose(iz, falta));
  p.entregueAntes = p.entregue;
  z.estado = IRRIG_ATIVA;
  z.inicioIrrigacao = millis();
  z.duracaoAtual = falta;
  z.confirmacoes = 0;
  z.origemDose = "retomada";
  ultimoCheckpoint[iz] = millis();
  salvarIrrigacao();
  escrever(atuadores[z.indiceAtuador], true, "retomada");
}

// Maquina de estados da irrigacao da zona: fecha a dose no tempo e dispara doses automaticas.
void processarIrrigacao(int iz) {
  ZonaIrrig& z = zonas[iz];
  Atuador& v = atuadores[z.indiceAtuador];
  DosePersistida& p = persist[iz];

  if (z.estado == IRRIG_ATIVA) {
    unsigned long dec = millis() - z.inicioIrrigacao;
    if (dec >= z.duracaoAtual) {
      escrever(v, false, z.origemDose);
      z.fimIrrigacao = millis();
      z.estado = IRRIG_ESPERA;
      encerrarDose(iz);
    } else if (p.ativa && millis() - ultimoCheckpoint[iz] >= INTERVALO_CHECKPOINT) {
      ultimoCheckpoint[iz] = millis();
      p.entregue = p.entregueAntes + dec;
      salvarIrrigacao();
    }
    return;
  }
  if (z.estado == IRRIG_ESPERA) {
    if (millis() - z.fimIrrigacao >= janelaCega) z.estado = IRRIG_OCIOSO;
    return;
  }

  // nenhuma dose comeca durante a janela de medicao nem com a outra valvula aberta
  if (estadoJanela != JAN_INATIVA || outraValvulaAberta(iz)) return;

  if (p.retomar) { retomarDose(iz); return; }

  if (v.modo != AUTO) { z.confirmacoes = 0; return; }

  unsigned long duracao = duracaoDose(iz);
  if (duracao == 0) return;

  if (millis() < z.bloqueadoAte) { z.confirmacoes = 0; return; }
  if (z.fimIrrigacao > 0 && millis() - z.fimIrrigacao < intervaloMinimoIrrig) return;

  if (estrategia[iz] == ESTR_AGENDADO) {
    if (millis() - z.inicioAgendamento >= intervaloAgendado[iz]) {
      if (supervisaoUmidade && solosaturado(iz)) {
        marcarAgendamento(iz);
        registrarLinha('E', "irrig_pulada", "seguranca");
        return;
      }
      iniciarDose(iz, duracao, "logica");
    }
  } else {
    int leitura = (iz == 0) ? umidadeControleCorr1 : umidadeControleCorr2;
    if (leitura < 0) { z.confirmacoes = 0; return; }
    if (leitura >= limiarIrrigar[iz]) {
      if (z.ultimaMedicaoContada != totalMedicoes) {
        z.confirmacoes++;
        z.ultimaMedicaoContada = totalMedicoes;
      }
    } else {
      z.confirmacoes = 0;
      z.ultimaMedicaoContada = totalMedicoes;
    }
    if (z.confirmacoes >= confirmacoesNecessarias) iniciarDose(iz, duracao, "logica");
  }
}

// Fecha a valvula aberta por mais tempo que o maximo permitido, inclusive em manual.
void protegerValvulas() {
  for (int iz = 0; iz < 2; iz++) {
    Atuador& v = atuadores[zonas[iz].indiceAtuador];
    if (!v.ligado) continue;
    unsigned long desde = zonas[iz].inicioIrrigacao;
    if (desde > 0 && millis() - desde >= TEMPO_MAX_VALVULA) {
      v.manualLigado = false;
      zonas[iz].estado = IRRIG_ESPERA;
      zonas[iz].fimIrrigacao = millis();
      escrever(v, false, "seguranca");
      encerrarDose(iz);
      textoAlerta = String(v.nome) + " excedeu o tempo maximo de abertura e "
                    "foi fechada automaticamente.";
    }
  }
}
