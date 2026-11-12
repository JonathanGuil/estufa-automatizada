from pydantic import field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Configuracao lida das variaveis de ambiente (infra/env/.env.<ambiente>).

    Segredos NAO tem valor padrao: se faltarem, o backend nao sobe. Assim nenhuma
    senha fica no codigo e um .env incompleto aparece na hora, no log do container.
    """
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    env: str = "prod"
    device_id: str = "estufa01"

    database_url: str                 # obrigatorio (contem a senha do Postgres)

    mqtt_host: str = "mosquitto"
    mqtt_port: int = 1883
    mqtt_user: str = "backend"
    mqtt_pass: str                    # obrigatorio

    auth_user: str = "admin"
    auth_pass: str                    # obrigatorio; vale ate a senha ser trocada pelo painel
    session_secret: str               # obrigatorio
    session_max_age: int = 60 * 60 * 12  # 12 h

    # Alertas por e-mail (opcionais: sem SMTP_HOST ou ALERTA_EMAIL_PARA ficam desligados)
    smtp_host: str = ""
    smtp_port: int = 587                  # 587 = STARTTLS; 465 = SSL direto
    smtp_usuario: str = ""
    smtp_senha: str = ""
    alerta_email_de: str = ""             # vazio = usa o SMTP_USUARIO
    alerta_email_para: str = ""           # padrao; o e-mail salvo no painel (Conta) tem prioridade
    alerta_sem_dados_min: int = 15        # minutos sem telemetria ate avisar
    painel_url: str = ""                  # endereco do painel: link dos e-mails e do "Esqueci minha senha"

    @field_validator("database_url", "mqtt_pass", "auth_pass", "session_secret")
    @classmethod
    def _sem_placeholder(cls, v: str, info):
        if not v or "TROQUE" in v or v in ("changeme", "troque-este-segredo"):
            raise ValueError(f"{info.field_name.upper()} ainda esta com o valor de exemplo; defina no .env")
        return v

    @field_validator("session_secret")
    @classmethod
    def _segredo_longo(cls, v: str):
        if len(v) < 16:
            raise ValueError("SESSION_SECRET precisa de pelo menos 16 caracteres")
        return v


settings = Settings()
