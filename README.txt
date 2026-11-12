ESTUFA: BACKEND E PAINEL NA VM
==============================

Backend conteinerizado para o controlador de estufa (TCC). Recebe a telemetria do ESP32
via MQTT, grava no Postgres, expõe uma API e um painel web com login, e permite ajustar
setpoints remotamente (empurrados de volta ao ESP32). O loop de controle continua
autônomo no ESP32: pela VM é possível ajustar parâmetros e, em modo manual, ligar e
desligar equipamentos, mas as decisões automáticas e as travas de segurança ficam no ESP32.

Ambientes
---------
- prod: estufa.IP_DA_VM.sslip.io, device estufa01, dados reais
- hml: hml.IP_DA_VM.sslip.io, device estufa01-sim, testes e histórico
- Política (setpoints) é remota e configurável; mecanismo (controle + atuação) é local.

Dimensionamento: 2 vCPU / 3,3 GB / 40 GB livre comportam os dois ambientes 24/7 com
Postgres separados (uso real ~0,5 GB). mem_limit em cada serviço como guarda; se precisar
recuperar RAM, make hml-down.

Portas
------

Além da 22 (SSH), o firewall de entrada da VM precisa liberar:
- 443: painel (obrigatório)
- 80: emissão do certificado Let's Encrypt e redirecionamento para https
- 8883: MQTT do ESP32 (obrigatório para a telemetria chegar)

Instalar Docker (Ubuntu 26.04 "resolute")
-----------------------------------------

    sudo apt update && sudo apt install -y ca-certificates curl
    sudo install -m 0755 -d /etc/apt/keyrings
    sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
    sudo chmod a+r /etc/apt/keyrings/docker.asc
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] \
    https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo "$VERSION_CODENAME") stable" \
    | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
    sudo apt update
    sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
    sudo usermod -aG docker $USER    # vale a partir do próximo login

Subir o ambiente (primeira vez)
-------------------------------

    # 1. Copiar o repositório para a VM
    cd estufa-nuvem

    # 2. Configure os segredos
    cp infra/env/.env.prod.example infra/env/.env.prod
    cp infra/env/.env.hml.example  infra/env/.env.hml
    #   trocar as senhas TROQUE_... nos dois arquivos
    #   e o e-mail do Let's Encrypt em infra/Caddyfile

    # 3. Rede + certificados do MQTT + usuários do broker + imagem
    make net
    make certs IP=IP_DA_VM                       # gera infra/mosquitto/certs (ca.crt vai p/ firmware)
    make passwd BACKEND_PW='senha-mqtt-backend' ESP32_PW='senha-mqtt-esp32'
    make build

    # 4. Suba infra e ambientes
    make infra-up
    make prod-up
    make hml-up
    make ps

O painel de produção fica em https://estufa.IP_DA_VM.sslip.io
(hml em https://hml.IP_DA_VM.sslip.io). Login = AUTH_USER/AUTH_PASS do .env; depois
de trocada no painel, vale a senha nova.

Testar sem o ESP32
------------------

    docker run --rm --network estufa_net eclipse-mosquitto:2 \
      mosquitto_pub -h mosquitto -p 1883 -u backend -P 'senha-mqtt-backend' \
      -t estufa/estufa01/telemetria \
      -m '{"temp":24.5,"u1":1100,"u2":1300,"uctrl1":1100,"uctrl2":1300,"ucorr1":800,"ucorr2":1000,"atuadores":{"vent":0,"aquec":0,"exaus":0,"led":1,"valv1":0,"valv2":0,"manual":{}},"origem":"teste"}'
A leitura aparece no painel de prod. No hml, o tópico é estufa/estufa01-sim/telemetria.

Importar os CSV antigos
-----------------------

O importador (backend/tools/import_csv.py) lê as colunas pelo nome, no cabeçalho gravado
pelo firmware, e por padrão pula o que já está no banco (epoch até o maior já gravado):

    docker cp log.csv backend-hml:/tmp/log.csv
    docker exec -it backend-hml python -m tools.import_csv \
      --arquivo /tmp/log.csv --device-id estufa01-sim
    # --dry-run mostra a contagem sem gravar

Segredos: o que fica fora do Git
--------------------------------

Nenhuma senha fica no código. Os valores reais ficam em arquivos ignorados pelo Git
(.gitignore); no repositório vão só os modelos.

- infra/env/.env.prod, infra/env/.env.hml (como criar: copiar de infra/env/.env.prod.example / .env.hml.example): senhas do Postgres e do MQTT do backend, login do painel, SESSION_SECRET e, se usar alertas, a do e-mail
- infra/mosquitto/passwd (como criar: make passwd): hashes dos usuários backend e esp32 do broker
- infra/mosquitto/certs/*.key (como criar: make certs): chaves privadas da CA e do broker
- firmware/estufa_v14/secrets.h (como criar: copiar de firmware/estufa_v14/secrets.example.h): Wi-Fi, OTA e MQTT do ESP32

O backend não tem senha padrão: se faltar DATABASE_URL, MQTT_PASS, AUTH_PASS ou
SESSION_SECRET no .env (ou se ainda estiverem com TROQUE_...), ele não sobe e o log diz
qual variável falta.

Para trocar as senhas do painel e do MQTT do backend por senhas novas geradas na própria VM:
bash infra/scripts/rotacionar_segredos.sh. A senha do painel
gerada pelo script substitui a que tiver sido trocada no painel.

Atualizar a VM com uma versão nova
----------------------------------

No PC, gere um pacote só com o código (sem .env, passwd, certs nem secrets.h), envie e publique.
Com o projeto no Git, git archive inclui só o que está versionado, então os segredos ficam de fora:

    git archive --format=tar.gz --prefix=estufa-nuvem/ -o estufa-nuvem_vN.tgz HEAD
    scp estufa-nuvem_vN.tgz <usuario>@<IP_DA_VM>:~
    ssh <usuario>@<IP_DA_VM> "tar -xzf ~/estufa-nuvem_vN.tgz -C /tmp estufa-nuvem/infra/scripts/publicar.sh && bash /tmp/estufa-nuvem/infra/scripts/publicar.sh ~/estufa-nuvem_vN.tgz"

O painel é servido direto da pasta frontend/ pelo Caddy. Ao mudar o CSS ou o JS, aumente o
?v= das tags no index.html, para o navegador não continuar com a versão antiga em cache.
O código do painel usa a formatação padrão do Prettier; depois de editar, rode
npx prettier --write frontend na raiz do projeto.

O publicar.sh faz backup da pasta (~/estufa-nuvem_backup_<data>) e do banco
(backups/prod_antes_publicacao_<data>.sql.gz), testa no hml (se estiver rodando), troca a
produção e confere saúde e conexão MQTT. Se algo falhar, ele volta sozinho para a versão anterior.
Para voltar manualmente depois: bash infra/scripts/reverter.sh ~/estufa-nuvem_backup_<data>.

Operação
--------

    make prod-logs      # logs do backend de produção
    make hml-logs
    make backup         # pg_dump do prod -> backups/*.sql.gz
    make prod-down / hml-down / infra-down

Alertas por e-mail
------------------

O backend avisa por e-mail quando o log local do ESP32 tem problema (log_cheio, log_falha,
log_descartado, log_quase_cheio), quando uma dose interrompida não é completada
(dose_abandonada) e quando a telemetria para de chegar por mais de ALERTA_SEM_DADOS_MIN
minutos (e de novo quando volta). O mesmo tipo de alerta sai no máximo a cada 6 h. O painel
mostra os mesmos alertas numa faixa no topo.

Para ligar, preencha no infra/env/.env.prod as variáveis SMTP_* e PAINEL_URL (ver
.env.prod.example) e recrie o backend. Os e-mails vão para o e-mail da conta, definido no painel
(Conta); enquanto ele não for salvo, vale o ALERTA_EMAIL_PARA do .env.
Com Gmail: SMTP_HOST=smtp.gmail.com, porta 587 e uma senha de app. Teste do envio:

    docker exec backend-prod python -m app.alertas --teste

No hml, deixe SMTP_HOST vazio.

Conta, senha e e-mail
---------------------

O painel tem um usuário só (AUTH_USER). Na seção Conta:
- E-mail da conta: recebe os alertas e o link de "Esqueci minha senha". Trocar pede a senha
  atual. O botão "Enviar e-mail de teste" confere o SMTP.
- Senha: troca pedindo a senha atual (mínimo de 8 caracteres). As outras sessões abertas são
  encerradas.

Esqueci minha senha (na tela de login): a pessoa digita o e-mail; se for o da conta, chega um
link que vale 30 minutos e funciona uma vez só. A resposta é a mesma para qualquer e-mail, para não
revelar qual está cadastrado, e cada IP pode pedir 3 vezes a cada 15 minutos. Precisa do SMTP e do
PAINEL_URL no .env.prod; no hml, sem SMTP, a tela avisa que a recuperação não está configurada.

A senha fica no banco, com hash scrypt. O AUTH_PASS do .env vale até a primeira troca. Sem
acesso ao e-mail, a senha também pode ser definida direto na VM:

    docker exec -it backend-prod python -m app.senha                 # pede a senha nova
    docker exec -it backend-prod python -m app.senha --usar-env      # volta a valer o AUTH_PASS do .env
    docker exec -it backend-prod python -m app.senha --email nome@exemplo.com

Estrutura
---------

    estufa-nuvem/
      Makefile                  # atalhos (net, build, certs, passwd, *-up/down, backup)
      .gitignore / .gitattributes # segredos e dados fora do Git; LF em todos os textos
      MQTT_CONTRATO.txt          # contrato de tópicos e payloads entre o ESP32 e a VM
      infra/
         docker-compose.yml         # Caddy + Mosquitto (infra compartilhada)
         docker-compose.prod.yml    # Postgres + backend (produção)
         docker-compose.hml.yml     # Postgres + backend (homologação)
         Caddyfile                  # TLS automático + reverse proxy /api + estático
         mosquitto/{mosquitto.conf,acl,gen-ca.sh}
         env/.env.{prod,hml}.example
         scripts/{publicar,reverter,rotacionar_segredos}.sh
      backend/                  # FastAPI: ingest MQTT + API + login + config
         app/{main,config,db,models,schemas,auth,api,mqtt_worker,alertas,conta,senha}.py
         tools/import_csv.py
      frontend/                 # painel: visão geral, Clima, Irrigação, Iluminação, Registro
         index.html             # estrutura da página
         css/painel.css
         js/                    # um arquivo por parte (api e formatação, atuadores, leituras,
                                 # dados, gráficos, eventos, configurações, navegação, conta,
                                 # alertas)
      firmware/
         NOTAS_MQTT.txt           # como compilar e como o ESP32 fala com a VM
         estufa_v14/             # firmware do ESP32: .ino + um módulo .h/.cpp por subsistema
                                  # (credenciais em secrets.h, fora do Git)

Próximos passos
---------------
- Endurecer faixas dos setpoints na API (hoje a validação forte fica no ESP32).
