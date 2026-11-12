"""Senha e e-mail da conta do painel pela linha de comando (para quando o e-mail nao ajuda).

Uso, na VM:
    docker exec -it backend-prod python -m app.senha               # pede a senha nova
    docker exec -it backend-prod python -m app.senha --usar-env    # volta a valer o AUTH_PASS do .env
    docker exec -it backend-prod python -m app.senha --email nome@exemplo.com
Trocar a senha encerra as sessoes abertas no painel.
"""
import argparse
import getpass
import sys

from . import conta, models  # noqa: F401  (models registra as tabelas)
from .config import settings
from .db import Base, engine


def main() -> int:
    p = argparse.ArgumentParser(prog="python -m app.senha",
                                description="Senha e e-mail da conta do painel.")
    g = p.add_mutually_exclusive_group()
    g.add_argument("--usar-env", action="store_true",
                   help="apaga a senha gravada no banco; volta a valer o AUTH_PASS do .env")
    g.add_argument("--email", help="troca o e-mail da conta (alertas e recuperacao de senha)")
    a = p.parse_args()

    Base.metadata.create_all(bind=engine)   # tabelas da conta, caso o backend ainda nao tenha subido

    if a.usar_env:
        conta.usar_senha_do_env()
        print("Pronto: vale de novo o AUTH_PASS do .env. As sessoes abertas foram encerradas.")
        return 0

    if a.email:
        endereco = conta.normalizar_email(a.email)
        if not endereco:
            print("E-mail invalido (use um endereco so).", file=sys.stderr)
            return 1
        conta.definir_email(endereco)
        print(f"E-mail da conta: {endereco}")
        return 0

    nova = getpass.getpass("Senha nova: ")
    erro = conta.validar_senha_nova(nova)
    if erro:
        print(erro, file=sys.stderr)
        return 1
    if getpass.getpass("Repita a senha: ") != nova:
        print("As senhas nao conferem.", file=sys.stderr)
        return 1
    conta.definir_senha(nova)
    print(f"Senha do usuario {settings.auth_user} trocada. As sessoes abertas foram encerradas.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
