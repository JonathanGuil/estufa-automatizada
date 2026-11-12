from typing import Literal

from pydantic import BaseModel, ConfigDict, Field

# Setpoints: estrutura conhecida, mas 'extra=allow' deixa a firmware
# evoluir sem quebrar a API. A validação forte de faixas fica na firmware
# (clamp defensivo no ESP32) + endurecimento futuro aqui.


class Setpoints(BaseModel):
    model_config = ConfigDict(extra="allow")

    # Setpoints termicos dia/noite e irrigacao por zona (arrays [z1, z2]).
    # Ver MQTT_CONTRATO.txt.
    versao: int | None = None
    fotoperiodo: dict | None = None    # {"liga_h": 6, "desliga_h": 21}
    aquecedor: dict | None = None      # {"liga_dia_c":20,"desliga_dia_c":22,"liga_noite_c":13.5,"desliga_noite_c":15.5}
    ventilador: dict | None = None     # idem: liga_dia_c / desliga_dia_c / liga_noite_c / desliga_noite_c
    exaustor: dict | None = None       # idem
    circulacao: dict | None = None     # {"habilitado":bool,"hora_ini_h":int,"hora_fim_h":int,
                                       #  "liga_min":float,"desliga_min":float,"aciona_exaustor":bool}
    irrigacao: dict | None = None      # {"dose_ml":[a,b], "intervalo_h":[a,b], "vazao_ml_min":[a,b],
                                       #  "limiar_saturado_adc":[a,b], "supervisao_umidade":bool,
                                       #  "dose_manual_reinicia_intervalo":bool}


# Acionamento remoto (comando/set). Por atuador, com os ids do firmware
# (vent, aquec, exaus, led, valv1, valv2). Comando parcial e valido: manda so
# os atuadores que mudam. 'extra=forbid' rejeita ids desconhecidos.
class AtuadorCmd(BaseModel):
    model_config = ConfigDict(extra="forbid")
    modo: Literal["auto", "manual"] | None = None
    ligado: bool | None = None


class Comando(BaseModel):
    model_config = ConfigDict(extra="forbid")
    vent: AtuadorCmd | None = None
    aquec: AtuadorCmd | None = None
    exaus: AtuadorCmd | None = None
    led: AtuadorCmd | None = None
    valv1: AtuadorCmd | None = None
    valv2: AtuadorCmd | None = None


# Conta do painel (senha e e-mail). Os limites de tamanho so barram abusos; as regras da
# senha (minimo de caracteres) ficam em conta.py, com mensagem propria para o painel.
class PedidoLink(BaseModel):
    email: str = Field(max_length=254)


class Redefinicao(BaseModel):
    token: str = Field(min_length=20, max_length=200)
    senha: str = Field(max_length=200)


class TrocaEmail(BaseModel):
    email: str = Field(max_length=254)
    senha_atual: str = Field(max_length=200)


class TrocaSenha(BaseModel):
    senha_atual: str = Field(max_length=200)
    nova: str = Field(max_length=200)
