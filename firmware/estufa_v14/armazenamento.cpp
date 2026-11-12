// Log CSV no LittleFS: um arquivo por dia, retencao e limpeza.

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <math.h>
#include <time.h>
#include "armazenamento.h"
#include "hora.h"
#include "sensores.h"
#include "janela.h"
#include "atuadores.h"
#include "controle.h"
#include "mqtt.h"
#include "configuracao.h"

// Arquivos e formato do log
const char* DIR_LOG        = "/log";       // um arquivo por dia: /log/AAAAMMDD.csv
const char* ARQ_LOG_ANTIGO = "/log.csv";   // arquivo unico do formato anterior, migrado no boot
const char* ARQ_TMP        = "/log.tmp";

const char* CABECALHO = "tipo,epoch,datahora,temp,umid1,umid2,uctrl1,uctrl2,"
                        "ucorr1,ucorr2,"
                        "vent,aquec,exaus,led,valv1,valv2,"
                        "mvent,maquec,mexaus,mled,mvalv1,mvalv2,evento,origem";

const int    LOG_DIAS_MAX     = 3;          // arquivos diarios mantidos, contando o de hoje
const size_t LIMITE_LOG_BYTES = 900000UL;   // teto da soma dos arquivos
const float  FS_ALERTA = 0.85;              // ocupacao da particao que gera log_quase_cheio
const float  FS_REARME = 0.80;
const float  FS_LIBERAR = 0.90;            // so apaga arquivo para gravar se a particao passar disto
const unsigned long INTERVALO_CHECAGEM_FS = 600000UL;

// Estado do log
bool fsPronto = false;
bool logCheio = false;   // a ultima gravacao falhou mesmo depois de liberar espaco
long linhasLog = 0, descartadosSemHora = 0, falhasGravacao = 0;
String ultimoRegistro = "nenhum";
unsigned long ultimoEpochGravado = 0;
String arquivoAtual = "";
bool alertaOcupacao = false;
unsigned long ultimaChecagemFs = 0;

// Registro periodico
unsigned long ultimoLog = 0;
unsigned long intervaloLog = 300000UL;
const unsigned long INTERVALO_LOG_MIN = 5000UL;
const unsigned long INTERVALO_LOG_MAX = 3600000UL;

// Eventos gerados pelo proprio log, registrados depois da linha em curso
struct EventoPendente { const char* tipo; const char* origem; String extra; };
const int MAX_PENDENTES = 6;
EventoPendente pendentes[MAX_PENDENTES];
int  nPendentes = 0;
bool emitindoPendentes = false;

// Caminho do arquivo do dia (hora local) de um epoch.
String arquivoDoDia(uint32_t ep) {
  time_t t = (time_t)ep; struct tm l; localtime_r(&t, &l);
  char b[24]; strftime(b, sizeof(b), "/log/%Y%m%d.csv", &l);
  return String(b);
}

// Nome do arquivo sem a pasta.
String nomeBase(const String& caminho) {
  return caminho.substring(caminho.lastIndexOf('/') + 1);
}

// Data dd/mm a partir do nome AAAAMMDD.csv.
String dataDoArquivo(const String& caminho) {
  String n = nomeBase(caminho);
  if (n.length() < 8) return n;
  return n.substring(6, 8) + "/" + n.substring(4, 6);
}

// Lista os arquivos do log em ordem cronologica; retorna quantos encontrou.
int listarArquivosLog(String* caminhos, int maximo) {
  if (!fsPronto) return 0;
  File dir = LittleFS.open(DIR_LOG);
  if (!dir || !dir.isDirectory()) return 0;
  int n = 0;
  File f = dir.openNextFile();
  while (f) {
    String nome = nomeBase(String(f.name()));
    bool arquivo = !f.isDirectory();
    f.close();
    if (arquivo && nome.endsWith(".csv") && n < maximo) {
      String c = String(DIR_LOG) + "/" + nome;
      int i = n++;
      while (i > 0 && caminhos[i - 1] > c) { caminhos[i] = caminhos[i - 1]; i--; }
      caminhos[i] = c;
    }
    f = dir.openNextFile();
  }
  dir.close();
  return n;
}

// Epoch do campo 'epoch' de uma linha do CSV (0 se nao houver).
uint32_t epochDaLinha(const String& linha) {
  int v1 = linha.indexOf(',');
  if (v1 < 0) return 0;
  int v2 = linha.indexOf(',', v1 + 1);
  if (v2 <= v1) return 0;
  return (uint32_t) strtoul(linha.substring(v1 + 1, v2).c_str(), nullptr, 10);
}

// Epoch da ultima linha de um arquivo (le so o final).
uint32_t ultimoEpochArquivo(const String& caminho) {
  File f = LittleFS.open(caminho, "r");
  if (!f) return 0;
  size_t sz = f.size();
  const size_t JAN = 512;
  f.seek(sz > JAN ? sz - JAN : 0);
  String bloco = f.readString();
  f.close();
  bloco.trim();
  int nl = bloco.lastIndexOf('\n');
  return epochDaLinha((nl >= 0) ? bloco.substring(nl + 1) : bloco);
}

// Epoch da primeira linha de dados de um arquivo.
uint32_t primeiroEpochArquivo(const String& caminho) {
  File f = LittleFS.open(caminho, "r");
  if (!f) return 0;
  f.readStringUntil('\n');
  String l = f.readStringUntil('\n');
  f.close();
  return epochDaLinha(l);
}

// Linhas de dados de um arquivo (sem o cabecalho).
long linhasDoArquivo(const String& caminho) {
  File f = LittleFS.open(caminho, "r");
  if (!f) return 0;
  uint8_t buf[512];
  long nl = 0;
  while (f.available()) {
    size_t k = f.read(buf, sizeof(buf));
    for (size_t j = 0; j < k; j++) if (buf[j] == '\n') nl++;
  }
  f.close();
  return nl > 0 ? nl - 1 : 0;
}

// Conta as linhas de dados de todos os arquivos.
void contarLinhas() {
  String arqs[MAX_ARQUIVOS_LOG];
  int n = listarArquivosLog(arqs, MAX_ARQUIVOS_LOG);
  linhasLog = 0;
  for (int i = 0; i < n; i++) linhasLog += linhasDoArquivo(arqs[i]);
}

// Cria o arquivo so com o cabecalho.
bool criarArquivo(const String& caminho) {
  File f = LittleFS.open(caminho, "w");
  if (!f) return false;
  bool ok = f.println(CABECALHO) > 0;
  f.close();
  return ok;
}

// Move o log de arquivo unico (formato anterior) para a pasta, nomeado pelo dia da primeira linha.
void migrarLogAntigo() {
  if (!LittleFS.exists(ARQ_LOG_ANTIGO)) return;
  uint32_t ep = primeiroEpochArquivo(ARQ_LOG_ANTIGO);
  if (ep < 1700000000UL) { LittleFS.remove(ARQ_LOG_ANTIGO); return; }
  String base = arquivoDoDia(ep);
  String destino = base;
  for (char suf = 'a'; LittleFS.exists(destino) && suf <= 'z'; suf++)
    destino = base.substring(0, base.length() - 4) + String(suf) + ".csv";
  if (LittleFS.rename(ARQ_LOG_ANTIGO, destino))
    Serial.printf("Log antigo movido para %s\n", destino.c_str());
  else
    Serial.println("Falha ao mover o log antigo; ele continua em /log.csv");
}

// Monta o LittleFS, migra o log antigo e carrega marca d'agua e parametros.
void iniciarFs() {
  fsPronto = LittleFS.begin(true);
  if (!fsPronto) { Serial.println("LittleFS: falha ao montar."); return; }
  if (!LittleFS.exists(DIR_LOG)) LittleFS.mkdir(DIR_LOG);
  migrarLogAntigo();
  contarLinhas();
  Serial.printf("LittleFS ok | %ld registros | arquivos: %s\n", linhasLog, resumoArquivosLog().c_str());
  carregarMarca();
  carregarConfig();
}

// Guarda um evento do log para ser registrado depois da linha em curso.
void agendarEvento(const char* tipo, const char* origem, const String& extra) {
  if (nPendentes >= MAX_PENDENTES) return;
  pendentes[nPendentes].tipo = tipo;
  pendentes[nPendentes].origem = origem;
  pendentes[nPendentes].extra = extra;
  nPendentes++;
}

// Registra os eventos guardados por agendarEvento.
void emitirPendentes() {
  if (emitindoPendentes || nPendentes == 0) return;
  emitindoPendentes = true;
  int limite = 10;
  while (nPendentes > 0 && limite-- > 0) {
    EventoPendente e = pendentes[0];
    for (int i = 1; i < nPendentes; i++) pendentes[i - 1] = pendentes[i];
    nPendentes--;
    registrarLinha('E', e.tipo, e.origem, e.extra);
  }
  emitindoPendentes = false;
}

// Apaga um arquivo antigo e avisa se ele ainda nao tinha chegado a VM.
bool apagarArquivoAntigo(const String& caminho) {
  bool enviado = ultimoEpochArquivo(caminho) <= mqttMarca;
  long nl = linhasDoArquivo(caminho);
  String extra = "\"arquivo\":\"" + nomeBase(caminho) + "\"";
  if (!LittleFS.remove(caminho)) {
    textoAlerta = "Falha ao apagar " + nomeBase(caminho) + " do log local.";
    agendarEvento("log_falha", "sistema", extra);
    return false;
  }
  linhasLog -= nl;
  if (linhasLog < 0) linhasLog = 0;
  agendarEvento(enviado ? "log_apagado" : "log_descartado", "sistema", extra);
  return true;
}

// Mantem no maximo LOG_DIAS_MAX arquivos e LIMITE_LOG_BYTES no total, apagando os mais antigos.
void aplicarRetencao() {
  String arqs[MAX_ARQUIVOS_LOG];
  size_t tam[MAX_ARQUIVOS_LOG];
  int n = listarArquivosLog(arqs, MAX_ARQUIVOS_LOG);
  size_t total = 0;
  for (int i = 0; i < n; i++) {
    File f = LittleFS.open(arqs[i], "r");
    tam[i] = f ? f.size() : 0;
    if (f) f.close();
    total += tam[i];
  }
  for (int i = 0; i < n - 1 && (n - i > LOG_DIAS_MAX || total > LIMITE_LOG_BYTES); i++) {
    if (arqs[i] == arquivoAtual || !apagarArquivoAntigo(arqs[i])) break;
    total -= tam[i];
  }
}

// Apaga o arquivo mais antigo (nunca o de hoje) para abrir espaco, se a particao estiver quase cheia.
bool liberarEspaco() {
  size_t total = LittleFS.totalBytes();
  if (total == 0 || LittleFS.usedBytes() < total * FS_LIBERAR) return false;
  String arqs[MAX_ARQUIVOS_LOG];
  int n = listarArquivosLog(arqs, MAX_ARQUIVOS_LOG);
  if (n < 2 || arqs[0] == arquivoAtual) return false;
  return apagarArquivoAntigo(arqs[0]);
}

// Avisa uma vez quando a particao passa de FS_ALERTA; rearma abaixo de FS_REARME.
void verificarOcupacao() {
  if (ultimaChecagemFs != 0 && millis() - ultimaChecagemFs < INTERVALO_CHECAGEM_FS) return;
  ultimaChecagemFs = millis();
  size_t total = LittleFS.totalBytes();
  if (total == 0) return;
  float uso = (float)LittleFS.usedBytes() / (float)total;
  if (!alertaOcupacao && uso >= FS_ALERTA) {
    alertaOcupacao = true;
    agendarEvento("log_quase_cheio", "sistema", "\"uso_pct\":" + String((int)(uso * 100)));
  } else if (alertaOcupacao && uso < FS_REARME) {
    alertaOcupacao = false;
  }
}

// Grava a linha no arquivo do dia; na virada do dia cria o arquivo novo e aplica a retencao.
bool gravarLinhaLog(const String& linha, uint32_t ep) {
  if (!fsPronto) return false;
  String alvo = arquivoDoDia(ep);
  if (alvo != arquivoAtual) {
    if (!LittleFS.exists(alvo)) criarArquivo(alvo);
    arquivoAtual = alvo;
    aplicarRetencao();
  }
  bool ok = false;
  for (int tentativa = 0; tentativa < 2 && !ok; tentativa++) {
    if (tentativa > 0 && !liberarEspaco()) break;
    if (!LittleFS.exists(alvo) && !criarArquivo(alvo)) continue;
    File f = LittleFS.open(alvo, "a");
    if (!f) continue;
    ok = f.println(linha) == linha.length() + 2;
    f.close();
  }
  if (!ok) {
    falhasGravacao++;
    if (!logCheio) {
      logCheio = true;
      textoAlerta = "Falha ao gravar no log local; os dados seguem so para a VM.";
      agendarEvento("log_cheio", "sistema");
    }
    return false;
  }
  if (logCheio) {
    logCheio = false;
    if (textoAlerta.startsWith("Falha ao gravar")) textoAlerta = "";
    agendarEvento("log_normalizado", "sistema");
  }
  linhasLog++;
  verificarOcupacao();
  return true;
}

// Reescreve um arquivo mantendo so as linhas com epoch depois do corte.
bool filtrarArquivo(const String& caminho, unsigned long corte) {
  File o = LittleFS.open(caminho, "r");
  if (!o) return false;
  File t = LittleFS.open(ARQ_TMP, "w");
  if (!t) { o.close(); return false; }
  t.println(CABECALHO);
  o.readStringUntil('\n');
  bool erro = false;
  while (o.available()) {
    String linha = o.readStringUntil('\n');
    linha.trim();
    if (linha.length() < 3) continue;
    if (epochDaLinha(linha) > corte && t.println(linha) == 0) { erro = true; break; }
  }
  o.close(); t.close();
  if (erro) { LittleFS.remove(ARQ_TMP); return false; }
  LittleFS.remove(caminho);
  return LittleFS.rename(ARQ_TMP, caminho);
}

// Apaga do log as linhas com epoch ate o corte (arquivos inteiros quando possivel).
void limparAteEpoch(unsigned long corte) {
  if (!fsPronto) return;
  String arqs[MAX_ARQUIVOS_LOG];
  int n = listarArquivosLog(arqs, MAX_ARQUIVOS_LOG);
  bool erro = false;
  for (int i = 0; i < n && !erro; i++) {
    if (ultimoEpochArquivo(arqs[i]) <= corte) {
      if (!LittleFS.remove(arqs[i])) erro = true;
      continue;
    }
    if (primeiroEpochArquivo(arqs[i]) <= corte) erro = !filtrarArquivo(arqs[i], corte);
    break;   // os arquivos seguintes sao todos mais novos
  }
  contarLinhas();
  if (erro) { textoAlerta = "Falha ao apagar ate epoch - log preservado."; return; }
  logCheio = false;
  ultimoRegistro = "apagado ate epoch " + String(corte);
}

// Apaga todos os arquivos do log.
void limparTudo() {
  if (!fsPronto) return;
  String arqs[MAX_ARQUIVOS_LOG];
  int n = listarArquivosLog(arqs, MAX_ARQUIVOS_LOG);
  for (int i = 0; i < n; i++) LittleFS.remove(arqs[i]);
  linhasLog = 0; descartadosSemHora = 0; falhasGravacao = 0;
  ultimoRegistro = "nenhum"; logCheio = false;
}

// Grava uma linha no log do dia e publica no MQTT, mesmo se a gravacao falhar.
void registrarLinha(char tipo, const char* evento, const char* origem, const String& extra) {
  if (!horaValida) { descartadosSemHora++; return; }

  uint32_t ep = (uint32_t)time(nullptr);

  String tempTxt = (sensorPerdido || isnan(ultimaTempValida))
                     ? String("") : String(ultimaTempValida, 2);
  String c1 = (umidadeControle1 < 0) ? String("") : String(umidadeControle1);
  String c2 = (umidadeControle2 < 0) ? String("") : String(umidadeControle2);
  String k1 = (umidadeControleCorr1 < 0) ? String("") : String(umidadeControleCorr1);
  String k2 = (umidadeControleCorr2 < 0) ? String("") : String(umidadeControleCorr2);

  String linha = String(tipo) + "," + String(ep) + "," +
                 horaISO() + "," + tempTxt + "," +
                 String(umidade1) + "," + String(umidade2) + "," +
                 c1 + "," + c2 + "," + k1 + "," + k2;
  for (int i = 0; i < N_ATUADORES; i++)
    linha += "," + String(atuadores[i].ligado ? 1 : 0);
  for (int i = 0; i < N_ATUADORES; i++)
    linha += "," + String(atuadores[i].modo == MANUAL ? 1 : 0);
  linha += ","; linha += evento;
  linha += ","; linha += origem;

  bool gravou = gravarLinhaLog(linha, ep);
  if (gravou) {
    ultimoEpochGravado = ep;
    ultimoRegistro = horaFormatada() + "  (" + String(tipo) + ")";
  }
  publicarLinhaMqtt(tipo, evento, origem, ep, gravou, extra);
  emitirPendentes();
}

// Define o intervalo do registro periodico, dentro dos limites.
void setIntervaloLog(unsigned long ms) {
  if (ms < INTERVALO_LOG_MIN) ms = INTERVALO_LOG_MIN;
  if (ms > INTERVALO_LOG_MAX) ms = INTERVALO_LOG_MAX;
  intervaloLog = ms; ultimoLog = millis();
}

// Soma do tamanho dos arquivos do log em bytes.
size_t tamanhoLog() {
  String arqs[MAX_ARQUIVOS_LOG];
  int n = listarArquivosLog(arqs, MAX_ARQUIVOS_LOG);
  size_t s = 0;
  for (int i = 0; i < n; i++) {
    File f = LittleFS.open(arqs[i], "r");
    if (f) { s += f.size(); f.close(); }
  }
  return s;
}

// Resumo dos arquivos para o painel local, ex.: "3 de 3 (22/10 a 24/10)".
String resumoArquivosLog() {
  String arqs[MAX_ARQUIVOS_LOG];
  int n = listarArquivosLog(arqs, MAX_ARQUIVOS_LOG);
  if (n == 0) return "nenhum";
  String s = String(n) + " de " + String(LOG_DIAS_MAX) + " (" + dataDoArquivo(arqs[0]);
  if (n > 1) s += " a " + dataDoArquivo(arqs[n - 1]);
  return s + ")";
}
