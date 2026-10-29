# Casa Dashboard — Painel residencial com ESP32 e Home Assistant

Painel físico para controle de dispositivos residenciais Sonoff usando **ESP32-WROOM-32**, botões pushbuttom e **LCD 1602 I²C**. O painel conversa com o Home Assistant pela rede local; o Home Assistant gerencia os dispositivos compatíveis por meio da integração instalada.

> **Estado do projeto:** firmware organizado para usar somente três botões (S1, S2 e S3). O exemplo de configuração contém entidades específicas de uma instalação residencial; quem reutilizar o projeto deve substituí-las pelas próprias entidades do Home Assistant.

## Sumário

- [Funcionalidades](#funcionalidades)
- [Arquitetura](#arquitetura)
- [Hardware e ligações](#hardware-e-ligações)
- [Como funciona a navegação](#como-funciona-a-navegação)
- [Requisitos](#requisitos)
- [1. Instalar o Home Assistant OS em uma máquina virtual no Linux Mint](#1-instalar-o-home-assistant-os-em-uma-máquina-virtual-no-linux-mint)
- [2. Configurar o Home Assistant](#2-configurar-o-home-assistant)
- [3. Integrar os dispositivos Sonoff](#3-integrar-os-dispositivos-sonoff)
- [4. Identificar as entidades](#4-identificar-as-entidades)
- [5. Criar um token para o ESP32](#5-criar-um-token-para-o-esp32)
- [6. Preparar a Arduino IDE](#6-preparar-a-arduino-ide)
- [7. Configurar `secrets.h`](#7-configurar-secretsh)
- [8. Compilar e gravar o firmware](#8-compilar-e-gravar-o-firmware)
- [Personalizar ou adicionar dispositivos](#personalizar-ou-adicionar-dispositivos)
- [Diagnóstico de problemas](#diagnóstico-de-problemas)
- [Segurança](#segurança)
- [Limitações e próximos passos](#limitações-e-próximos-passos)
- [Licença e contribuições](#licença-e-contribuições)

## Funcionalidades

- Controla três dispositivos configurados como exemplo:
  - interruptor Sonoff da Sala;
  - interruptor Sonoff da Cozinha;
  - lâmpada Sonoff do Quarto.
- Usa apenas três botões físicos: **S1, S2 e S3**.
- Exibe no LCD o menu e o estado do dispositivo selecionado.
- Consulta os estados dos dispositivos ao Home Assistant.
- Envia comandos `toggle` pela API REST do Home Assistant.
- Exibe hora e data obtidas por NTP, com fuso horário UTC−3.
- Consulta condição climática e temperatura de uma entidade `weather` do Home Assistant.
- Após cinco segundos sem interação, apresenta um ticker com hora, data e clima.
- Tenta restabelecer a conexão Wi-Fi quando ela cai.
- Mantém credenciais em `secrets.h`, separado do arquivo principal.
- Usa ArduinoJson para interpretar as respostas JSON.
- Mantém o firmware em um arquivo `.ino`, com funções separadas por responsabilidade e cadastro dos dispositivos centralizado.

O painel **não controla Sonoff diretamente pela conta eWeLink**. O fluxo é ESP32 → Wi-Fi → Home Assistant → integração do dispositivo. O aplicativo eWeLink pode continuar disponível, conforme o modelo, o firmware e a integração usados.

## Arquitetura

```text
┌─────────────────────┐       rede local       ┌──────────────────────┐
│ ESP32-WROOM-32      │  ───── Wi-Fi/HTTP ───► │ Home Assistant OS    │
│ Button: S1, S2, S3   │  ◄──── estados ─────── │ em máquina virtual   │
│ LCD 1602 I²C        │                        └──────────┬───────────┘
└─────────────────────┘                                   │
                                                   integração Sonoff
                                                          │
                                           ┌──────────────┼─────────────┐
                                           ▼              ▼             ▼
                                         Sala          Cozinha        Quarto
```

A máquina virtual deve estar conectada à mesma rede local que o ESP32. Para este cenário, a opção recomendada no VirtualBox é **Adaptador em modo Bridge/Placa em modo bridge**, em vez de NAT, para que o ESP32 possa alcançar diretamente o IP do Home Assistant.

## Hardware e ligações

### Componentes

- ESP32-WROOM-32.
- Placa HC-35S com botões físicos.
- LCD 1602 com adaptador I²C.
- Cabos de ligação e alimentação adequada.
- Rede Wi-Fi de 2,4 GHz acessível pelo ESP32.
- Computador com Linux Mint para executar a máquina virtual.

### LCD 1602 I²C

| LCD | ESP32 |
|---|---:|
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| GND | GND |
| VCC | Conforme as especificações do módulo e os níveis elétricos compatíveis com o ESP32 |

O endereço I²C configurado por padrão é `0x27`. Se o seu módulo utilizar outro endereço, altere `LCD_ADDRESS` no arquivo `.ino`.

**Atenção à alimentação:** muitos adaptadores I²C de LCD são alimentados em 5 V e podem elevar SDA/SCL para 5 V, o que não é seguro para os GPIOs do ESP32. Verifique o adaptador e utilize níveis lógicos compatíveis (por exemplo, conversor de nível quando necessário).

### Botões — somente três

| Botão | GPIO | Função |
|---|---:|---|
| S1 | 26 | Descer no menu; na tela inicial, abre o menu pelo primeiro item |
| S2 | 27 | Subir no menu; na tela inicial, abre o menu pelo último item |
| S3 | 32 | Abrir o menu na tela inicial ou acionar o item selecionado |

O firmware lê somente GPIO 26, 27 e 32. O código configura os pinos dos botões como `INPUT`, portanto a montagem deve fornecer níveis lógicos estáveis. Confirme o circuito elétrico dos push button antes de alterar para `INPUT_PULLUP` ou modificar as ligações.

## Como funciona a navegação

### Menu de dispositivos

- **S1:** avança para o próximo item; após o último, retorna ao primeiro.
- **S2:** volta para o item anterior; antes do primeiro, vai para o último.
- **S3:** envia o comando `toggle` para o dispositivo selecionado, se estiver configurado.

### Tela inicial

- **S1:** abre o menu selecionando o primeiro item.
- **S2:** abre o menu selecionando o último item.
- **S3:** abre o menu.

### Tela informativa

Depois de cinco segundos sem interação, o LCD mostra um texto deslizante com hora, data, condição climática e temperatura. Qualquer um dos três botões interrompe o ticker e retorna à tela inicial. O botão pressionado para sair do ticker **não** aciona um dispositivo; pressione novamente para executar outra ação.

Como o painel tem apenas três botões, não existe um botão dedicado a “voltar” enquanto o menu está ativo. O retorno à tela inicial ocorre ao sair do ticker; após novo período de inatividade, o ticker volta a aparecer.

## Requisitos

### Hardware e rede

- Computador com Linux Mint e virtualização habilitada no BIOS/UEFI.
- Memória suficiente para executar o sistema anfitrião e a VM. Como referência prática, reserve pelo menos 2 GB para a VM; 4 GB é preferível se o computador permitir.
- VirtualBox ou outro hipervisor compatível com a imagem oficial do Home Assistant OS.
- ESP32 e computador/VM na mesma rede local.
- Acesso ao Home Assistant pelo navegador.
- Dispositivos Sonoff já adicionados à conta eWeLink, quando aplicável.

### Software

- Home Assistant OS (recomendado para esta instalação em VM).
- Integração compatível com os dispositivos Sonoff. Este README descreve SonoffLAN, mas o método exato pode variar conforme modelo, firmware e versão da integração.
- Arduino IDE.
- Pacote de placas ESP32 da Espressif.
- Biblioteca `LiquidCrystal_I2C` compatível com o LCD.
- Biblioteca **ArduinoJson**, compatível com a API usada pelo firmware (ArduinoJson 6 é uma escolha adequada para este código).

---

## 1. Instalar o Home Assistant OS em uma máquina virtual no Linux Mint

Este guia usa **VirtualBox** como exemplo. Os nomes das opções podem variar conforme a versão do VirtualBox e do Linux Mint.

### 1.1 Preparar o computador

1. Confirme que a virtualização está habilitada no BIOS/UEFI (Intel VT-x ou AMD-V/SVM).
2. Instale o VirtualBox usando a documentação oficial correspondente à sua versão do Linux Mint/Ubuntu.
3. Reinicie o computador, se a instalação ou os módulos do VirtualBox solicitarem.
4. Confira se o VirtualBox abre e consegue criar uma máquina virtual.

Documentação oficial:
- Home Assistant — instalação em máquina virtual: https://www.home-assistant.io/installation/
- VirtualBox: https://www.virtualbox.org/wiki/Downloads

### 1.2 Baixar a imagem oficial

1. Abra a página oficial de instalação do Home Assistant.
2. Escolha a instalação **Home Assistant OS em VirtualBox**.
3. Baixe a imagem indicada nessa página para VirtualBox. Use a imagem oficial correspondente ao tipo de processador do computador.
4. Se a documentação oferecer um arquivo compactado, extraia-o antes de selecionar o disco na máquina virtual.

Não use uma imagem de Raspberry Pi em uma VM de PC. Se a página oficial mudar o formato ou o procedimento, siga a instrução atual da documentação oficial para VirtualBox.

### 1.3 Criar a máquina virtual

1. Abra o VirtualBox e clique em **Novo**.
2. Dê um nome como `Home Assistant`.
3. Se a versão do VirtualBox perguntar o tipo de sistema, selecione Linux/Other Linux de 64 bits, conforme as opções disponíveis. Se a imagem oficial exigir uma configuração específica, siga a documentação dela.
4. Reserve pelo menos 2 GB de RAM; se possível, use 4 GB.
5. Reserve 2 CPUs virtuais, se o computador tiver recursos disponíveis.
6. Na etapa de disco, escolha **usar um disco virtual existente** e selecione a imagem oficial baixada, quando esse for o formato fornecido. Não crie um disco vazio no lugar da imagem do Home Assistant OS.
7. Antes de iniciar, abra **Configurações → Sistema** e ajuste a ordem de inicialização conforme necessário.
8. Em **Configurações → Rede**, configure o Adaptador 1 como:
   - **Habilitar placa de rede**;
   - **Conectado a: Placa em modo bridge/Adaptador em modo Bridge**;
   - selecione a placa física que realmente conecta o Linux Mint à rede (Ethernet ou Wi-Fi).
9. Salve as configurações e inicie a VM.

**Por que usar bridge?** Com NAT, o Home Assistant pode ficar atrás de uma rede privada da VM e o ESP32 pode não conseguir alcançá-lo diretamente. Em modo bridge, a VM normalmente recebe um IP da mesma rede do roteador, como outro dispositivo da casa. Alguns adaptadores Wi-Fi ou redes corporativas podem limitar bridge; se isso acontecer, ajuste a rede com conhecimento das implicações ou consulte a documentação do hipervisor.

### 1.4 Primeiro acesso

1. Deixe a VM iniciar e aguarde a configuração inicial. A primeira inicialização pode levar vários minutos.
2. No roteador, consulte a lista de clientes DHCP e procure o novo dispositivo/IP; ou veja o endereço exibido no console da VM.
3. No navegador do Linux Mint, tente:
   - `http://homeassistant.local:8123`
   - ou `http://IP_DA_VM:8123`, substituindo pelo IP real.
4. Conclua o assistente inicial para criar a conta de administrador e definir a localização, fuso horário e unidades.
5. Confirme que a interface do Home Assistant abre antes de continuar.

Se `homeassistant.local` não resolver, use o IP. A resolução `.local` depende da rede e do serviço de descoberta; não é necessária se o IP funcionar.

### 1.5 Manter o IP acessível

Para que o ESP32 continue encontrando o servidor após reinicializações:

1. No roteador, crie uma **reserva DHCP** para a VM usando o MAC da placa de rede virtual; ou configure uma reserva equivalente no seu equipamento de rede.
2. Reinicie a rede/VM se necessário e confirme que o IP permanece estável.
3. Anote o IP, pois ele será usado em `HA_URL` no arquivo `secrets.h`.

Uma reserva DHCP no roteador costuma ser mais simples que configurar um IP manual dentro do Home Assistant.

### 1.6 Iniciar automaticamente

Se desejar que o Home Assistant suba junto com o computador, configure a inicialização automática da VM nas opções do VirtualBox ou do sistema anfitrião. Lembre-se de que o Home Assistant ficará indisponível quando o computador estiver desligado, suspenso ou sem rede.

---

## 2. Configurar o Home Assistant

1. Abra o Home Assistant pelo navegador.
2. Conclua o assistente inicial.
3. Acesse **Configurações → Dispositivos e serviços**.
4. Verifique se a integração de clima já criou uma entidade `weather`. Caso não exista, configure uma integração de clima compatível com sua região e confirme que ela fornece condição e temperatura.
5. Confirme que a VM tem acesso à internet para instalar integrações e obter atualizações.
6. Faça um backup do Home Assistant antes de instalar integrações personalizadas ou atualizar componentes.

O firmware utiliza por padrão a entidade `weather.forecast_casa`. Essa entidade é apenas o nome configurado para a instalação de origem; ela **não aparece automaticamente em toda instalação**. Se sua entidade tiver outro ID, altere `WEATHER_ENTITY` no `.ino`.

## 3. Integrar os dispositivos Sonoff

O processo depende do modelo do Sonoff, de seu firmware e do método de integração suportado.

### 3.1 Preparar os Sonoff

1. Configure os dispositivos no eWeLink, se o modelo exigir essa etapa.
2. Confirme que cada dispositivo funciona pelo aplicativo antes de integrá-lo ao Home Assistant.
3. No Home Assistant, abra **Configurações → Dispositivos e serviços** e verifique se já existe uma integração oficial compatível com o seu dispositivo.
4. Se não houver suporte adequado e o dispositivo for compatível com SonoffLAN, prossiga com a instalação abaixo.

### 3.2 Instalar HACS

O SonoffLAN é uma integração personalizada e normalmente é instalado por meio do HACS. Siga primeiro o procedimento oficial atualizado do HACS para Home Assistant OS:

https://www.hacs.xyz/docs/use/

Em linhas gerais:

1. Verifique os pré-requisitos e instale o HACS conforme a documentação oficial.
2. Reinicie o Home Assistant quando solicitado.
3. Conclua a configuração do HACS e a autorização exigida.
4. Confirme que a seção HACS aparece na interface.

O processo de instalação do HACS pode mudar entre versões; use a documentação oficial em vez de copiar comandos antigos de tutoriais não atualizados.

### 3.3 Instalar SonoffLAN pelo HACS

Repositório do projeto:
https://github.com/AlexxIT/SonoffLAN

1. Abra o HACS no Home Assistant.
2. Entre em **Integrações** e procure por **Sonoff** ou **SonoffLAN**.
3. Se o repositório não aparecer na pesquisa, consulte as instruções atuais do projeto SonoffLAN para adicioná-lo como repositório personalizado no HACS. O endereço do repositório é `https://github.com/AlexxIT/SonoffLAN`.
4. Instale a integração e reinicie o Home Assistant quando solicitado.
5. Vá para **Configurações → Dispositivos e serviços → Adicionar integração**.
6. Procure a integração Sonoff/SonoffLAN e siga o assistente disponível na versão instalada.
7. Informe as credenciais ou autorizações solicitadas pelo procedimento atual e conclua a configuração.
8. Verifique se os dispositivos aparecem e se seus estados mudam ao controlá-los pelo Home Assistant.

**Importante:** a configuração do SonoffLAN mudou ao longo do tempo e alguns modelos/firmwares podem exigir métodos diferentes ou não ser compatíveis. Consulte o README e as notas da versão instalada no repositório oficial.

### 3.4 Testar cada dispositivo

Antes de conectar o ESP32, teste cada dispositivo pela interface do Home Assistant:

1. Abra a página do dispositivo ou a entidade correspondente.
2. Ligue e desligue o dispositivo.
3. Confirme que o estado exibido pelo Home Assistant acompanha o estado físico.
4. Se o controle não funcionar pelo Home Assistant, resolva a integração primeiro. O painel ESP32 depende dela.

O eWeLink pode continuar sendo usado em paralelo, mas a disponibilidade e a sincronização dependem do modelo e da integração.

## 4. Identificar as entidades

O firmware não identifica os Sonoff automaticamente; ele usa IDs de entidade definidos no código.

1. No Home Assistant, abra **Ferramentas de desenvolvedor → Estados** (o nome pode variar conforme a versão).
2. Procure o dispositivo desejado.
3. Copie o ID exato da entidade, incluindo o domínio:
   - `switch.nome_do_interruptor`
   - `light.nome_da_lampada`
4. Confirme que o estado é `on` ou `off`.
5. Anote também o ID da entidade de clima, que deve começar com `weather.` e apresentar o atributo `temperature`.

Os IDs que aparecem no firmware original são exemplos da instalação em que ele foi desenvolvido. Em outra casa, substitua-os pelos IDs reais.

## 5. Criar um token para o ESP32

O ESP32 autentica na API REST do Home Assistant com um **Long-Lived Access Token**.

1. No Home Assistant, abra o perfil do usuário administrador.
2. Localize a seção de tokens de acesso de longa duração.
3. Crie um token com um nome identificável, por exemplo `Casa Dashboard ESP32`.
4. Copie o token imediatamente e guarde-o em local privado. A interface pode não mostrá-lo novamente.
5. Não inclua o token no README, em capturas de tela, em logs públicos ou no GitHub.

O token permite executar ações com as permissões do usuário associado. Sempre que possível, use uma conta dedicada com permissões limitadas, se a configuração e as permissões disponíveis na sua instalação permitirem.

## 6. Preparar a Arduino IDE

### 6.1 Instalar suporte à placa ESP32

1. Instale a Arduino IDE.
2. Nas preferências, configure o índice de placas da Espressif conforme a documentação oficial: https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html
3. Abra o Gerenciador de Placas.
4. Instale o pacote **esp32 by Espressif Systems**.
5. Conecte a placa ao computador por USB e selecione o modelo ESP32 apropriado e a porta serial correta.

### 6.2 Instalar bibliotecas

No Gerenciador de Bibliotecas da Arduino IDE, instale:

- `ArduinoJson` — o código usa `StaticJsonDocument` e `deserializeJson`.
- `LiquidCrystal_I2C` — escolha uma versão compatível com o módulo LCD utilizado.

`WiFi.h`, `HTTPClient.h` e `Wire.h` são fornecidas pelo pacote de placas ESP32.

## 7. Configurar `secrets.h`

O arquivo principal inclui `secrets.example.h`, que precisa estar na pasta do sketch. Crie-o ao lado de `Casa_Dashboard.ino` com o seguinte conteúdo e substitua os valores:

```cpp
#pragma once

const char* WIFI_SSID = "NOME_DA_REDE_WIFI";
const char* WIFI_PASSWORD = "SENHA_DA_REDE_WIFI";

const char* HA_URL = "http://192.168.1.100:8123";
const char* HA_TOKEN = "COLE_SEU_TOKEN_AQUI";
```

- `WIFI_SSID`: nome exato da rede Wi-Fi.
- `WIFI_PASSWORD`: senha da rede Wi-Fi.
- `HA_URL`: endereço do Home Assistant acessível na rede local, incluindo `http://` e a porta `8123`. Não coloque `/` no final.
- `HA_TOKEN`: token criado na etapa anterior.

Substitua `192.168.1.100` pelo IP real da máquina virtual. Não use `localhost` ou `127.0.0.1`: no ESP32, esses endereços apontariam para o próprio ESP32, não para o computador.


## 8. Compilar e gravar o firmware

1. Crie uma pasta chamada `Casa_Dashboard`.
2. Coloque nela `Casa_Dashboard.ino` e `secrets.h`.
3. Abra `Casa_Dashboard.ino` na Arduino IDE.
4. Confira se as bibliotecas estão instaladas.
5. Selecione a placa ESP32 e a porta serial.
6. Compile antes de gravar.
7. Grave o firmware.
8. Abra o Monitor Serial em **115200 baud**.
9. Observe as mensagens de Wi-Fi, NTP, clima e comunicação com o Home Assistant.
10. Teste LCD, botões e cada dispositivo.

Se a compilação falhar, leia a primeira mensagem de erro relevante; erros posteriores podem ser apenas consequência do primeiro.

## Personalizar ou adicionar dispositivos

### Alterar o clima

No começo do `.ino`, altere:

```cpp
const char* WEATHER_ENTITY = "weather.forecast_casa";
```

Use o ID real da sua entidade de clima. A entidade precisa fornecer um estado de condição climática e o atributo `temperature`.

### Adicionar um Sonoff

Os dispositivos ficam centralizados no vetor `devices`, com nome, ID de entidade, domínio e indicação de configuração. Exemplo:

```cpp
{"Escritorio", "switch.entidade_do_escritorio",
 DeviceDomain::SWITCH, true}
```

Para uma lâmpada, use `DeviceDomain::LIGHT` e uma entidade `light.*`.

Ao adicionar um item:

1. confirme que a entidade funciona pelo Home Assistant;
2. inclua a configuração no vetor `devices`;
3. confira o domínio correto (`SWITCH` ou `LIGHT`);
4. compile e grave o firmware;
5. teste leitura do estado e acionamento.

Os itens `Reserva` têm `configured = false` e `entityId = nullptr`. Substitua um deles pelos dados reais ao utilizá-lo. O firmware percorre os itens configurados e os reservados no menu.

### Comandos disponíveis

O firmware usa o serviço `toggle` para o domínio do dispositivo:

- `switch` → `/api/services/switch/toggle`
- `light` → `/api/services/light/toggle`

O estado não é invertido localmente como se o comando já tivesse sido confirmado. Depois do comando, o firmware consulta novamente o estado ao Home Assistant.

## Diagnóstico de problemas

### ESP32 não conecta ao Wi-Fi

- Confira SSID e senha em `secrets.h`.
- Confirme que a rede 2,4 GHz está disponível para o ESP32.
- Verifique a intensidade do sinal.
- Confirme se o roteador permite que os dispositivos da rede local se comuniquem.

### Home Assistant não responde

- Abra `http://IP_DA_VM:8123` no navegador de outro dispositivo da mesma rede.
- Confirme o IP e a porta `8123`.
- Confirme que a VM está ligada e conectada por bridge.
- Verifique `HA_URL` e `HA_TOKEN`.
- Veja o Monitor Serial em 115200 baud.
- Se o token foi revogado ou expirou, crie outro e atualize `secrets.h`.

### A entidade retorna erro ou o dispositivo não muda

- Confirme o ID exato em Ferramentas de desenvolvedor → Estados.
- Verifique se o domínio é `switch` ou `light`.
- Teste o serviço no próprio Home Assistant.
- Confirme se o dispositivo continua online e se a integração Sonoff está funcionando.

### Clima indisponível

- Verifique se `WEATHER_ENTITY` corresponde a uma entidade `weather` existente.
- Confirme se a entidade apresenta o atributo `temperature`.
- Algumas integrações podem demorar para obter os primeiros dados; confirme o estado na interface do Home Assistant.

### Hora aparece como `--:--`

- Verifique a conexão Wi-Fi e o acesso à internet/DNS.
- Aguarde a sincronização NTP.
- O firmware usa o fuso `BRT3` (UTC−3); ajuste a configuração se estiver em outro fuso.

### LCD vazio ou caracteres incorretos

- Verifique alimentação, GND, SDA (GPIO 21) e SCL (GPIO 22).
- Confirme o endereço I²C (`0x27` por padrão).
- Confira a biblioteca `LiquidCrystal_I2C`.
- Verifique a compatibilidade elétrica dos sinais I²C com o ESP32.

### Botões não respondem corretamente

- Confira os pinos GPIO 26, 27 e 32.
- O código atual usa `INPUT` e considera o botão pressionado quando o pino vai para `LOW`. Se a sua placa tiver outro circuito, será necessário adaptar a configuração após verificar o esquema elétrico.

## Segurança

- Nunca publique `secrets.h` nem um token válido.
- Não exponha a porta `8123` diretamente à internet.
- Mantenha o Home Assistant e suas integrações atualizados e faça backups.
- Use uma rede Wi-Fi protegida e, se possível, uma rede/VLAN separada para dispositivos IoT.
- O firmware utiliza HTTP para a API local. Em uma rede local confiável, isso pode ser suficiente para um protótipo, mas o token não é criptografado pelo HTTP. Considere HTTPS com validação adequada ou uma arquitetura de rede que proteja a comunicação.
- Não compartilhe logs que contenham dados privados ou credenciais.

## Limitações e próximos passos

- O firmware é um projeto de referência e pode exigir ajustes de hardware, biblioteca, entidade e integração para funcionar em outras instalações.
- A comunicação HTTP é síncrona; durante falhas de rede, a resposta dos botões pode atrasar até o timeout configurado.
- O nome e os IDs das entidades Sonoff/clima são específicos da instalação de origem e devem ser adaptados.
- O ticker usa uma tela LCD de 16×2 e apresenta os dados em texto corrido.
- Os itens reservados são placeholders, não dispositivos já integrados.
- Antes de qualquer atualização, guarde uma cópia do firmware que funciona e teste cada mudança individualmente.

## Licença e contribuições

Escolha e adicione um arquivo `LICENSE` antes de declarar uma licença pública para o projeto. Se publicar contribuições, descreva o hardware utilizado, a versão do Home Assistant, a integração Sonoff e as bibliotecas testadas. Não inclua credenciais, tokens ou informações privadas nos relatos de problemas.

## By THIAGO GOIS - V1.0