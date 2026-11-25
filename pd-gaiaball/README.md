# pd-gaiaball — objeto `[gaiaball]` para Pure Data

Os dados da Gaiaball dentro do Pure Data, no Windows, no Linux e no macOS. Quem conversa
com a bola por Bluetooth é a **config-app**, aberta no Chrome ou no Edge. Ela repassa os
pacotes ao objeto `[gaiaball]`, que publica cada valor num nome: no seu patch, basta um
`[r gaia.lat]`, `[r gaia.co2]`, `[r gaia.breath]`… Não há terminal, Python nem programa
extra: só o Pd e o navegador. Nada é escrito na bola; o objeto só escuta.

> **Primeira vez com a bola ou com o Pd?** Comece pelo [`TUTORIAL.md`](TUTORIAL.md): um
> passo a passo para músicos, sem jargão, com todos os dados (o que são, unidade, faixa e
> quantas vezes por segundo chegam), receitas e seis patches prontos na pasta
> [`exemplos/`](exemplos/).

```
┌──────────┐  Bluetooth  ┌───────────────────┐  WebSocket local   ┌────────────┐
│ Gaiaball │ ──────────> │ config-app        │ ─────────────────> │ [gaiaball] │──> [r gaia.*]
│ (ESP32)  │             │ (Chrome ou Edge)  │  127.0.0.1:9478    │   (no Pd)  │
└──────────┘             └───────────────────┘                    └────────────┘
```

Por que assim: o Pd não tem Bluetooth, e o Chrome tem (é o mesmo Bluetooth que a
config-app já usa para configurar a bola). A conexão entre a página e o Pd não sai do
computador.

## Como usar

### 1. Instalar o `[gaiaball]` pelo Deken (uma vez)

1. Abra o Pd (Pd vanilla 0.51 ou mais novo).
2. **Ajuda → Encontrar externals** (no Pd em inglês: *Help → Find externals*).
3. Procure `gaiaball` e clique em **Instalar**. Se o Pd perguntar se deve acrescentar a
   pasta ao caminho de busca, responda que sim.

**A busca não encontra o `gaiaball`?** Ele ainda não foi publicado no Deken. Baixe o pacote
do seu sistema (Windows, macOS ou Linux) na [página de versões](https://github.com/GaiaSenses/gaiaball/releases),
descompacte e siga o `LEIA-ME.html` que vem dentro: ele mostra onde pôr a pasta `gaiaball`
e, no Mac, como liberar o objeto. Não precisa de conta no GitHub.

### 2. Abrir a config-app e conectar a bola

1. Ligue a bola.
2. Abra a config-app no **Chrome** ou no **Edge**:
   <https://gaiasenses.github.io/gaiaball/>. O Firefox e o Safari não deixam sites usarem o
   Bluetooth.
3. Clique em **Conectar** e escolha a sua bola. Para enviar ao Pd não é preciso PIN.

A chave **Enviar ao Pd** (painel Conexão, bloco Pd) já vem ligada. Com ela ligada, a
config-app procura o Pd sozinha a cada 2 segundos, enquanto a bola estiver conectada.

### 3. Criar `[gaiaball]` no Pd

Crie um objeto `[gaiaball]` em qualquer patch e deixe esse patch aberto. Um só basta: os
nomes `gaia.*` valem para todos os patches abertos no mesmo Pd. Clique com o botão
direito no objeto → **Ajuda** para abrir o patch de ajuda, que mostra todos os canais
funcionando e tem um botão que abre a config-app.

- `[gaiaball]` escuta na porta 9478, a padrão da config-app.
- `[gaiaball 9500]` escuta na porta 9500. Nesse caso, mude também a porta na config-app
  (painel Conexão → Pd → Porta).
- Só um `[gaiaball]` por porta escuta de cada vez. Se já houver outro (por exemplo, o do
  patch de ajuda), o novo avisa `gaiaball: porta 9478 em uso` uma vez no console e espera:
  quando o primeiro for apagado ou o patch dele for fechado, o que esperava assume a porta
  sozinho, em até 2 segundos.

### 4. Liberar o Chrome (uma vez)

Na primeira conexão, o Chrome pergunta se o site pode acessar **Apps neste dispositivo**.
Clique em **Permitir**. Sem essa permissão, a página não consegue falar com o Pd, e o
indicador no alto da config-app mostra "Pd: bloqueado pelo Chrome".

Se você bloqueou sem querer: clique no ícone à esquerda do endereço → **Configurações do
site** → **Apps neste dispositivo** → **Permitir**, e recarregue a página.

Pronto: o indicador mostra "Pd: conectado · N pacotes/s", e o outlet do `[gaiaball]`
manda `connected 1`.

**Para testar sem bola:** abra a config-app com `?demo=1` no fim do endereço
(`https://gaiasenses.github.io/gaiaball/?demo=1`). Ela gera dados sintéticos e também
os envia ao Pd.

## Canais

Cada canal chega a cada pacote da sua origem: o movimento na taxa configurada na bola
(10 a 200 por segundo, padrão 50), a navegação no mesmo ritmo e o CO₂ cerca de uma vez
por segundo. Não há zona morta: para ignorar valores repetidos, use `[change]`.

| Canal | Valor | Faixa e unidade | Quando chega |
|---|---|---|---|
| `gaia.lat` | latitude do ponto para onde o globo virtual aponta | −90 a 90 graus (sul negativo) | navegação ativa na bola |
| `gaia.lon` | longitude desse ponto | −180 a 180 graus (oeste negativo) | navegação ativa |
| `gaia.speed` | velocidade de giro do globo | graus/s, de 0 para cima: no modo joystick vai até a velocidade máxima configurada na bola (padrão 90, no máximo 720); no modo espelho é a velocidade medida do giro, sem teto fixo | navegação ativa |
| `gaia.bearing` | rumo do mapa (padrão do Mapbox: sentido horário, a partir do norte) | −180 a 180 graus | navegação ativa |
| `gaia.settled` | globo parado | 0 ou 1 | navegação ativa |
| `gaia.acc.x`, `gaia.acc.y`, `gaia.acc.z` | aceleração em cada eixo | −4000 a 4000 mg (1000 mg = 1 g, a gravidade) | todo pacote de movimento |
| `gaia.quat` | orientação, como quatérnio: lista `w x y z` | cada valor de −1 a 1 | todo pacote de movimento |
| `gaia.mic.rms` | nível RMS dos microfones: lista `E D` (esquerdo, direito) | −120 a 0 dBFS (0 = fundo de escala; −120 = piso) | pacote de movimento, se os microfones funcionam |
| `gaia.mic.peak` | pico dos microfones: lista `E D` | −120 a 0 dBFS | pacote de movimento, se os microfones funcionam |
| `gaia.breath` | sopro reconhecido pela bola (1 enquanto dura) | 0 ou 1 | todo pacote de movimento |
| `gaia.sensors` | lista `roll pitch yaw aceX aceY aceZ co2` | ângulos em graus, aceleração em mg, CO₂ em ppm (0 até chegar o primeiro pacote de CO₂) | todo pacote de movimento |
| `gaia.co2` | CO₂ medido | ppm (ar livre fica perto de 420; sala cheia passa de 1000) | todo pacote de CO₂ |
| `gaia.co2.trigger` | gatilho de CO₂ da bola (o mesmo que abre as composições no site) | 0 ou 1 | todo pacote de CO₂ |

- `gaia.lat`, `gaia.lon`, `gaia.speed`, `gaia.acc.*`, `gaia.co2` e `gaia.sensors` têm os
  mesmos nomes e unidades que o site GaiaSenses manda ao patch. Os ângulos de
  `gaia.sensors` usam a mesma convenção do site. Um patch que só lê os valores roda igual
  nos dois.
- **Um evento por sopro:** `[r gaia.breath]` → `[change]` → `[sel 1]`.
- O áudio não chega em amostras: chega o nível já medido na bola (RMS e pico de cada
  microfone, a cada pacote de movimento). O espectro fica só na config-app.
- **Na desconexão**, `gaia.breath` e `gaia.co2.trigger` recebem 0 uma vez, para nenhum 1
  ficar preso no patch. Os outros canais mantêm o último valor.

## Outlet de status

| Mensagem | Quando |
|---|---|
| `connected 1` | a config-app conectou, ou os pacotes voltaram depois de um silêncio |
| `connected 0` | a config-app fechou a conexão, ou ficou 2 segundos sem mandar pacotes |
| `rate N` | uma vez por segundo, enquanto conectado: pacotes recebidos no último segundo (movimento + navegação + CO₂; com a bola no padrão, cerca de 100) |
| `gap N` | junto com o `rate`: o maior intervalo sem pacotes no último segundo, em milissegundos |

Uma conexão nova só substitui a anterior depois de completar o aperto de mão do
WebSocket: se duas abas da config-app tentarem enviar ao
mesmo tempo, vale a mais recente. Feche as outras.

## Solução de problemas

| Sintoma | Causa provável e o que fazer |
|---|---|
| O Pd diz `gaiaball ... couldn't create` | o objeto não está instalado ou a pasta não está no caminho do Pd. Instale pelo Deken ou pelo pacote da página de versões (passo 1) e confira em Preferências → Caminho. |
| O console diz `gaiaball: porta 9478 em uso` | outro `[gaiaball]` já escuta nessa porta (por exemplo, em outro patch aberto, ou no patch de ajuda). Não atrapalha: os `[r gaia.*]` recebem do que está escutando, e o outro assume a porta sozinho quando o primeiro for fechado. Se a porta estiver com outro programa, escolha outra porta nos dois lados: `[gaiaball 9500]` e a mesma porta na config-app. |
| A config-app mostra "Pd: aguardando" | o Pd não está aberto, não há `[gaiaball]` em nenhum patch aberto, ou as portas são diferentes nos dois lados. |
| A config-app mostra "Pd: bloqueado pelo Chrome" | a permissão "Apps neste dispositivo" foi negada. Libere como no passo 4 e recarregue a página. |
| A config-app mostra "Pd: desligado" | a chave Enviar ao Pd está desligada. |
| A config-app mostra "Pd: aguardando a bola" | a chave está ligada, mas a bola ainda não foi conectada na config-app: clique em **Conectar**. |
| `connected 1`, mas `gaia.lat` e `gaia.lon` não chegam | a navegação está desligada na bola. Na config-app, desbloqueie com o PIN e escolha a navegação Espelho ou Joystick. |
| `gaia.mic.*` não chega | os microfones não funcionaram quando a bola ligou (a bola deixa de mandar esses canais), ou o sensor de movimento está desligado (eles viajam no pacote de movimento). Desligar o "Detector de sopro" não para esses canais. Confira os microfones no painel "Sensores em tempo real" da config-app e, se preciso, reinicie a bola. |
| `rate` baixo ou `gap` alto | a página ficou lenta. Feche outras abas pesadas e deixe só uma aba da config-app enviando. |
| Ângulos "diferentes do site" | não são: é a mesma convenção. O zero e a montagem são ajustados **na bola**: use "Zerar orientação" na config-app (exige o PIN). |

## Segurança

O `[gaiaball]` só aceita conexões do próprio computador (`127.0.0.1`): nada de fora chega
nele pela rede. Ele aceita **qualquer página** que o Chrome autorizar em **Apps neste
dispositivo**, porque o endereço definitivo da config-app ainda não foi definido. Por isso,
**não dê essa permissão a sites que você não conhece**: um site autorizado poderia mandar
dados falsos ao seu patch.

Um navegador que não pede essa permissão deixa **qualquer página aberta** chegar ao
objeto. Uma página assim pode mandar dados falsos ou cortar os dados de verdade: ao
completar o aperto de mão do WebSocket, ela substitui a conexão anterior. Uma conexão
que não completa o aperto de mão (um `fetch`, uma imagem, uma varredura de portas) não
corta mais os dados: ela é descartada em 3 s e só uma fica esperando por vez. Por isso, durante uma apresentação, feche as páginas
que você não conhece.

## Para quem mantém o projeto

### Conteúdo da pasta

| Arquivo | O que é |
|---|---|
| `gaiaball.c` | a classe do Pd: servidor WebSocket só em `127.0.0.1`, sem threads (os sockets entram no laço do próprio Pd) |
| `src/gb_decode.{h,c}` | decodificador dos pacotes da bola, independente do transporte |
| `src/gb_ws.{h,c}`, `src/gb_sha1.{h,c}`, `src/gb_base64.{h,c}` | o protocolo WebSocket (RFC 6455) mínimo, sem sockets |
| `gaiaball-help.pd` | patch de ajuda (botão direito no objeto → Ajuda) |
| `Makefile` | compilação com o `pd-lib-builder` e o alvo `test` |
| `pd-lib-builder/` | submódulo git com o sistema de compilação de externals do Pd |
| `tests/` | testes em C do decodificador e do WebSocket; `tests/e2e/` tem o teste ponta a ponta com o Pd |
| [`PUBLICAR-NO-DEKEN.md`](PUBLICAR-NO-DEKEN.md) | como publicar uma versão nova no Deken |
| `pacotes/` | os pacotes de instalação da página de versões: `montar.sh` gera um `.zip` por sistema com o `LEIA-ME.html` (feito de `LEIA-ME.md`), o tutorial em HTML, a ajuda e os exemplos |
| [`PUBLICAR-NO-GITHUB.md`](PUBLICAR-NO-GITHUB.md) | como publicar uma versão nova na página de versões (Releases) do GitHub |

### Compilar e testar

```sh
git submodule update --init                       # traz o pd-lib-builder
make -C pd-gaiaball                               # compila o external
make -C pd-gaiaball PDINCLUDEDIR=<pure-data>/src  # se o m_pd.h não for achado sozinho
make -C pd-gaiaball floatsize=64 extension=linux-amd64-64.so   # Pd de precisão dupla
make -C pd-gaiaball test                          # testes em C; não precisam do Pd
```

O `PDINCLUDEDIR` pode apontar para a pasta `src/` do código-fonte do Pd
(`git clone --depth 1 --branch 0.55-2 https://github.com/pure-data/pure-data`). No
Windows, compile com o MSYS2 (MinGW64); o `Makefile` acrescenta o `ws2_32`.

**Ponta a ponta, sem bola e sem navegador** (Linux, com o Pd instalado):

```sh
pd -nogui -stderr -path pd-gaiaball -open pd-gaiaball/tests/e2e/listen.pd &
node pd-gaiaball/tests/e2e/send-goldens.mjs
```

O script faz o aperto de mão como o Chrome, manda os pacotes de referência e testa: a
segunda conexão substituindo a primeira, uma conexão TCP sem aperto de mão que não
derruba o cliente e é fechada em cerca de 3 s, o silêncio de 2 s, o fechamento, um quadro de
texto (recusado) e, por fim, a troca de objeto: ele apaga o primeiro `[gaiaball]` do
`listen.pd` (mensagem `clear` pela porta 9479) e confere que o segundo assume a porta.
Com `--spawn`, ele mesmo abre o Pd e confere tudo o que o `listen.pd` imprime:

```sh
node pd-gaiaball/tests/e2e/send-goldens.mjs --spawn pd -nogui -stderr \
  -path pd-gaiaball -open pd-gaiaball/tests/e2e/listen.pd
```

### Notas de projeto

- A fonte da verdade do protocolo é [`firmware/docs/protocol.md`](../firmware/docs/protocol.md).
  O decodificador confere versão e tipo, aceita pacotes maiores que o mínimo (campos
  novos são ignorados) e descarta o resto sem travar.
- A config-app repassa só os pacotes de movimento (`0x01`), CO₂ (`0x02`) e navegação
  (`0x07`), sem alterar, um por mensagem binária.
- O objeto não confere a origem da página (o domínio definitivo ainda não existe). A
  proteção é escutar só em `127.0.0.1` e o pedido "Apps neste dispositivo" do Chrome.
- A bola aceita duas conexões Bluetooth ao mesmo tempo: por exemplo, a config-app (que
  alimenta o Pd) e o site GaiaSenses.
