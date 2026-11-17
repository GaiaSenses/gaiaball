# Tutorial do `[gaiaball]`: a Gaiaball dentro do Pure Data

Este tutorial é para músicos e artistas que querem tocar com a Gaiaball no Pure Data
(o "Pd") e **nunca programaram**. Cada termo técnico é explicado na primeira vez que
aparece, e há um [glossário](#12-glossário) no fim.

Ao terminar, você vai ter instalado o objeto `[gaiaball]`, ligado a bola, visto os dados
chegarem e transformado movimento, sopro, CO₂ e o giro do globo em som.

> **Em uma frase:** a bola fala com o Chrome por Bluetooth, o Chrome repassa tudo ao Pd, e
> no seu patch cada dado chega num nome, como `[r gaia.breath]`.

## Sumário

1. [O que é a Gaiaball e o objeto `[gaiaball]`](#1-o-que-é-a-gaiaball-e-o-objeto-gaiaball)
2. [Do que você precisa](#2-do-que-você-precisa)
3. [Instalação passo a passo](#3-instalação-passo-a-passo)
4. [Primeira conexão passo a passo](#4-primeira-conexão-passo-a-passo)
5. [O objeto: argumento, entradas e saída](#5-o-objeto-argumento-entradas-e-saída)
6. [Todos os dados](#6-todos-os-dados)
7. [Taxas de atualização e o que isso significa para o som](#7-taxas-de-atualização-e-o-que-isso-significa-para-o-som)
8. [Modo palco e modo ajuste](#8-modo-palco-e-modo-ajuste)
9. [Receitas](#9-receitas)
10. [Exemplos prontos](#10-exemplos-prontos)
11. [Problemas comuns](#11-problemas-comuns)
12. [Glossário](#12-glossário)

---

## 1. O que é a Gaiaball e o objeto `[gaiaball]`

A **Gaiaball** é um globo de uns 40 cm que as pessoas giram, inclinam e sopram. Dentro
dela há:

- um **sensor de movimento** (o *IMU*): sabe para onde a bola está virada e sente quando
  ela é inclinada, empurrada ou chacoalhada;
- **dois microfones** (esquerdo e direito), usados para perceber o **sopro**;
- um **sensor de CO₂** (gás carbônico), que mede o ar em volta;
- um **botão giratório** (um *potenciômetro*), que ajusta a sensibilidade do sopro.

A bola também calcula sozinha um **globo virtual**: o ponto do mapa-múndi para onde um
globo estaria apontando se acompanhasse a bola. É isso que move o globo no site
GaiaSenses.

O Pd não tem Bluetooth. Quem conversa com a bola é a **config-app**, uma página que roda
no Chrome (ou no Edge). Ela repassa os dados ao objeto `[gaiaball]`, dentro do Pd:

```
┌──────────────┐
│   Gaiaball   │   a bola: movimento, microfones, CO₂, globo virtual
└──────┬───────┘
       │  Bluetooth (sem fio)
       ▼
┌────────────────────────────────┐
│  config-app, aberta no Chrome  │   https://gaiasenses.github.io/gaiaball/
└──────┬─────────────────────────┘
       │  WebSocket local: um "cano" de dados que não sai do seu computador
       │  (endereço 127.0.0.1, porta 9478)
       ▼
┌────────────────────────────────┐
│  objeto [gaiaball], no Pd      │
└──────┬─────────────────────────┘
       │  publica cada dado num nome
       ▼
 [r gaia.acc.x]   [r gaia.breath]   [r gaia.co2]   [r gaia.lat]   ...
```

Três coisas para guardar:

- **O objeto só escuta.** Nada do que você faz no Pd muda a bola.
- **Um `[gaiaball]` basta.** Os nomes `gaia.*` valem para todos os patches abertos no
  mesmo Pd.
- **`[r nome]` é a abreviação de `[receive nome]`**: um objeto que recebe tudo o que for
  enviado para aquele nome, de qualquer lugar do Pd. É como sintonizar uma rádio:
  `[r gaia.co2]` sintoniza a "estação" do CO₂.

## 2. Do que você precisa

- **A Gaiaball**, ligada.
- **Um computador com Bluetooth**, com Windows, macOS ou Linux.
- **Google Chrome ou Microsoft Edge.** O Firefox e o Safari não deixam sites usarem o
  Bluetooth.
- **Pure Data "vanilla", versão 0.51 ou mais nova.** *Vanilla* é o Pd oficial, baixado em
  <https://puredata.info/downloads>. Se você está começando, pegue a versão mais recente.
- **O objeto `[gaiaball]`** instalado (próxima seção).
- Fones ou caixas de som.

Não tem a bola por perto? Dá para treinar com o **modo demo** da config-app, que inventa
dados parecidos com os da bola ([seção 4, passo 9](#passo-9-opcional-treinar-sem-a-bola-o-modo-demo)).

## 3. Instalação passo a passo

Um *external* é um objeto extra, que não vem junto com o Pd. O `[gaiaball]` é um external.

### 3.1 Pelo Deken (o jeito normal)

O **Deken** é a "loja" de externals que fica dentro do próprio Pd.

1. Abra o Pd.
2. Menu **Ajuda → Encontrar externals** (no Pd em inglês: *Help → Find externals*).
3. Digite `gaiaball` e clique em **Instalar**.
4. Se o Pd perguntar se deve acrescentar a pasta ao caminho de busca, responda que sim.

### 3.2 Se a busca não encontrar o `gaiaball`

O `[gaiaball]` é novo, e colocá-lo no Deken é um passo manual da equipe (descrito em
[`PUBLICAR-NO-DEKEN.md`](PUBLICAR-NO-DEKEN.md)). Se a busca não mostra nada, ele ainda não
foi publicado. Enquanto isso, você tem três saídas:

**A) Pedir o arquivo à equipe do GaiaSenses.** É o mais simples.

**B) Baixar do GitHub.** O GitHub compila o objeto para todos os sistemas, mas só quando
alguém da equipe manda, e baixar exige uma conta no GitHub.

1. Entre no GitHub com a sua conta.
2. Abra <https://github.com/GaiaSenses/gaiaball/actions/workflows/pd-gaiaball.yml> e clique
   na execução mais recente que tem o sinal verde.
3. No fim da página, em **Artifacts**, baixe o arquivo do seu sistema:

   | Seu computador | Baixe | O arquivo que vem dentro |
   |---|---|---|
   | Windows | `gaiaball-bin-windows-32` | `gaiaball.dll` |
   | Mac (Intel ou Apple) | `gaiaball-bin-macos-32` | `gaiaball.pd_darwin` |
   | Linux, processador Intel ou AMD | `gaiaball-bin-linux-amd64-32` | `gaiaball.pd_linux` |
   | Linux, processador ARM de 64 bits | `gaiaball-bin-linux-arm64-32` | `gaiaball.linux-arm64-32.so` |

   O `-32` no fim é o Pd comum. Os arquivos terminados em `-64` são para o Pd de "precisão
   dupla", uma versão especial; se você não sabe qual tem, é o comum.
4. Descompacte o `.zip`.

Se não houver nenhuma execução, ou se os arquivos não aparecerem mais (o GitHub os apaga
depois de um tempo), peça a alguém da equipe para rodar de novo: **Actions → pd-gaiaball →
Run workflow**.

**C) Compilar você mesmo.** Para quem já compila programas: veja "Compilar e testar" no
[`README.md`](README.md#compilar-e-testar).

**Onde colocar o arquivo baixado (opções A e B):**

- **Jeito mais simples:** ponha o arquivo na mesma pasta do seu patch. O Pd procura objetos
  primeiro na pasta do patch. Ponha junto o `gaiaball-help.pd` (ele está na pasta
  [`pd-gaiaball`](https://github.com/GaiaSenses/gaiaball/tree/main/pd-gaiaball) do
  repositório), para a ajuda do objeto funcionar.
- **Para usar em qualquer patch:** crie uma pasta (por exemplo, `gaiaball`), ponha os
  arquivos nela e acrescente essa pasta em **Preferências → Caminho** (em inglês,
  *Preferences → Path*).

### 3.3 Conferir a instalação

1. Num patch vazio, aperte **Ctrl+1** (no Mac, **Cmd+1**) para criar um objeto.
2. Digite `gaiaball` e clique fora da caixa.
3. **Borda contínua:** deu certo. **Borda tracejada** e a mensagem
   `gaiaball ... couldn't create` no console (a janela principal do Pd, onde aparecem as
   mensagens): o Pd não achou o arquivo. Confira a pasta e o Caminho.

## 4. Primeira conexão passo a passo

Faça isto uma vez com calma. Nas próximas vezes bastam os passos 1, 2, 3, 4 e 8. A ordem
entre o Pd e a config-app não importa: os dois esperam um pelo outro.

### Passo 1. Ligue a bola parada

Apoie a bola numa superfície plana e só então ligue. **A posição em que ela liga vira a
"posição de referência"**: o zero da orientação e do globo virtual. A bola não tem uma
"frente" marcada; tudo é medido em relação a essa posição.

### Passo 2. Abra o Pd com um `[gaiaball]`

Abra o exemplo [`exemplos/01-ver-tudo.pd`](exemplos/01-ver-tudo.pd) (menu **Arquivo →
Abrir**) ou crie um `[gaiaball]` num patch seu. Deixe esse patch aberto.

### Passo 3. Abra a config-app no Chrome ou no Edge

Endereço (provisório): <https://gaiasenses.github.io/gaiaball/>. Atalho: na ajuda do
objeto (clique com o botão direito no `[gaiaball]` → **Ajuda**) há uma mensagem que abre a
página.

### Passo 4. Conecte a bola

No painel **Conexão**, clique em **Conectar**. O Chrome mostra uma lista de aparelhos:
escolha a sua bola (o nome de fábrica é `Globo_1`, mas pode ter sido trocado) e confirme.

### Passo 5. Só na primeira vez: ligue o sensor de movimento

**A bola sai de fábrica com o sensor de movimento (IMU) desligado, de propósito.** O mesmo
programa da bola serve para dois modelos de sensor, e ele não adivinha qual está dentro da
sua. Sem esse passo, só chegam ao Pd os dados de CO₂: nada de movimento, microfones, sopro
ou globo.

1. Ainda no painel **Conexão**, digite o **PIN** e clique em **Desbloquear**. O PIN é uma
   senha de 4 números que protege as configurações da bola; o de fábrica é `1234`. A bola
   não avisa se o PIN está errado: ela só ignora os pedidos.
2. No painel **Configuração**, aba **Básico**, campo **IMU** (no grupo *Aquisição*),
   escolha o modelo do sensor. Na bola atual do projeto é o **ICM-20948**. Na dúvida,
   pergunte a quem montou a bola.
3. Clique em **Aplicar**. A config-app envia, confere e grava na bola ("Aplicado,
   verificado e salvo no dispositivo"). Aparece o aviso *"A troca de IMU só vale depois de
   reiniciar a bola."*
4. Clique em **Reiniciar agora** e confirme. A conexão cai; isso é normal.
5. Com a bola parada na superfície, clique em **Conectar** de novo.
6. Para conferir: o jeito mais fácil é a linha no topo do painel **Configuração**, que
   deve dizer "Valores lidos da bola às HH:MM:SS · IMU em uso: ICM-20948". Em
   **Configuração → Avançado → Métricas**, a linha **Estado do IMU** deve dizer
   `ICM-20948 ok`. Se disser `selecionado, não encontrado` (ou a linha do topo mostrar
   "(não encontrado)"), o modelo escolhido não é o da sua bola: volte ao item 2 e escolha
   o outro.

A escolha fica gravada na bola: você não precisa repetir isto.

### Passo 6. Confira a chave "Enviar ao Pd"

No painel **Conexão**, bloco **Pd**: a chave **Enviar ao Pd** já vem ligada e a **Porta**
vem 9478. Com a bola conectada, a config-app procura o Pd sozinha a cada 2 segundos. Para
enviar ao Pd não é preciso PIN.

### Passo 7. Libere o Chrome (uma vez)

Na primeira vez, o Chrome pergunta se o site pode acessar **Apps neste dispositivo**.
Clique em **Permitir**. É a permissão para uma página conversar com programas do seu
computador, como o Pd.

Se você bloqueou sem querer: clique no ícone à esquerda do endereço → **Configurações do
site** → **Apps neste dispositivo** → **Permitir**, e recarregue a página.

### Passo 8. Confira se chegou

- No alto da config-app aparece **"Pd: conectado · N pacotes/s"**, com N perto de 100.
- No Pd, a saída do `[gaiaball]` manda `connected 1`. No `01-ver-tudo.pd`, o quadrado
  "conectado" acende e os números começam a mudar.

O texto no alto da config-app diz o que está acontecendo:

| A config-app mostra | Quer dizer |
|---|---|
| Pd: desligado | a chave Enviar ao Pd está desligada |
| Pd: aguardando a bola | a chave está ligada, mas a bola ainda não foi conectada |
| Pd: aguardando | a config-app procura o Pd e não acha: o Pd está fechado, não há `[gaiaball]` aberto ou as portas são diferentes |
| Pd: conectado · N pacotes/s | tudo certo |
| Pd: bloqueado pelo Chrome | a permissão "Apps neste dispositivo" foi negada (passo 7) |

### Passo 9 (opcional). Treinar sem a bola: o modo demo

Abra <https://gaiasenses.github.io/gaiaball/?demo=1> (o mesmo endereço com `?demo=1` no
fim). A config-app inventa dados parecidos com os da bola e os envia ao Pd pelo mesmo
caminho: o movimento 50 vezes por segundo, o globo 20 vezes por segundo e o CO₂ uma vez
por segundo, com um "sopro" a cada 6 segundos, o CO₂ perto de 430 ppm e o globo
alternando entre girar (uns 6 segundos) e parar (uns 4 segundos). Ótimo para testar os
exemplos. A permissão do passo 7 também vale aqui.

## 5. O objeto: argumento, entradas e saída

### O argumento: a porta

Uma **porta** é um número que identifica um programa dentro do computador. Pense num
prédio: o endereço `127.0.0.1` quer dizer "este computador" (o prédio), e a porta é o
número do apartamento onde o Pd atende.

- `[gaiaball]` atende na porta **9478**, a padrão da config-app.
- `[gaiaball 9500]` atende na porta 9500. Nesse caso, mude também a porta na config-app
  (painel **Conexão → Pd → Porta**). Os dois lados precisam ter o mesmo número.
- A porta tem de ser um número inteiro de 1 a 65535. Fora disso, o console mostra
  `gaiaball: porta inválida: ... (use um número de 1 a 65535)` e o objeto fica parado, sem
  fazer nada.
- Só troque a porta se a 9478 estiver ocupada por outro programa.

### Nenhuma entrada

A caixa `[gaiaball]` não tem entrada (nenhum ponto de ligação em cima): ela não recebe
mensagens. Para trocar a porta, edite o texto da caixa.

### Uma saída: o estado da conexão

A saída do `[gaiaball]` **não traz os dados da bola** (esses chegam pelos `[r gaia.*]`).
Ela conta como está a conexão:

| Mensagem | Quando chega |
|---|---|
| `connected 1` | a config-app conectou, ou os pacotes voltaram depois de um silêncio |
| `connected 0` | a config-app fechou a conexão, ou ficou **2 segundos** sem mandar nada |
| `rate N` | uma vez por segundo, enquanto conectado: quantos pacotes chegaram no último segundo (com a bola no padrão, cerca de 100) |
| `gap N` | junto com o `rate`: o maior intervalo sem pacotes no último segundo, em milissegundos (ms; 1000 ms = 1 segundo) |

Para separar as três mensagens, use `[route]`: ele olha a primeira palavra e manda o
número pela saída correspondente.

```
[gaiaball]
|
[route connected rate gap]
|            |         |
[tgl]       [ 101 ]   [ 24 ]
conectado   rate      gap (ms)
```

Na desconexão, `gaia.breath` e `gaia.co2.trigger` recebem 0 uma vez, para nenhum "1"
ficar preso no seu patch. Os outros canais ficam com o último valor. O exemplo
[`06-estado-da-conexao.pd`](exemplos/06-estado-da-conexao.pd) mostra tudo isso com avisos.

### Um `[gaiaball]` por porta

Só um `[gaiaball]` por porta atende de cada vez. Se outro patch aberto já tem um, o novo
avisa `gaiaball: porta 9478 em uso` uma vez no console e espera. Não atrapalha: os
`[r gaia.*]` recebem do que está atendendo. Quando o primeiro é apagado (ou o patch dele é
fechado), o que esperava assume a porta sozinho, em até 2 segundos. Por isso cada exemplo
tem o seu `[gaiaball]` e você pode abrir vários ao mesmo tempo.

### Uma conexão de cada vez e segurança

- O objeto só aceita conexões do próprio computador (`127.0.0.1`): nada de fora chega nele
  pela rede.
- Uma conexão nova substitui a anterior. Se duas abas da config-app tentarem enviar, vale
  a mais recente: deixe só uma aberta.
- O objeto aceita qualquer página que o Chrome autorizar em "Apps neste dispositivo". **Não
  dê essa permissão a sites que você não conhece**, e feche as páginas desconhecidas
  durante uma apresentação: uma página assim poderia mandar dados falsos ao seu patch.

## 6. Todos os dados

### 6.1 Cola rápida

"Vezes por segundo" vale para a bola no padrão de fábrica.

| Canal | O que é | Tipo | Unidade | Faixa típica | Vezes por segundo | Quando chega |
|---|---|---|---|---|---|---|
| `gaia.acc.x`, `gaia.acc.y`, `gaia.acc.z` | aceleração em cada eixo | número | mg | −4000 a 4000 (parada: perto de ±1000 no eixo vertical, perto de 0 nos outros) | 50 | todo pacote de movimento |
| `gaia.quat` | orientação, como quatérnio | lista de 4: `w x y z` | (sem unidade) | −1 a 1 cada | 50 | todo pacote de movimento |
| `gaia.sensors` | `roll pitch yaw aceX aceY aceZ co2` | lista de 7 | graus, mg, ppm | roll e yaw −180 a 180; pitch −90 a 90; aceleração e CO₂ como nos outros canais (co2 = 0 até o primeiro pacote de CO₂) | 50 | todo pacote de movimento |
| `gaia.mic.rms` | nível médio de cada microfone | lista de 2: `E D` | dBFS | −120 a 0 | 50 | pacote de movimento, se os microfones funcionam |
| `gaia.mic.peak` | pico de cada microfone | lista de 2: `E D` | dBFS | −120 a 0 | 50 | pacote de movimento, se os microfones funcionam |
| `gaia.breath` | sopro reconhecido pela bola | número | — | 0 ou 1 | 50 | todo pacote de movimento; vai a 0 na desconexão |
| `gaia.co2` | CO₂ do ar (ou o valor virtual do sopro) | número | ppm | ar livre perto de 420; 2100 logo depois de um sopro; 600 fixo com o sensor desligado | cerca de 1 | todo pacote de CO₂ |
| `gaia.co2.trigger` | gatilho de CO₂ | número | — | 0 ou 1 (liga acima de 1200 ppm, desliga em 1150 ou menos) | cerca de 1 | todo pacote de CO₂; vai a 0 na desconexão |
| `gaia.lat` | latitude do ponto do globo virtual | número | graus | −90 a 90 (sul negativo) | 50 | navegação ligada |
| `gaia.lon` | longitude desse ponto | número | graus | −180 a 180 (oeste negativo) | 50 | navegação ligada |
| `gaia.speed` | velocidade de giro do globo | número | graus por segundo | de 0 para cima | 50 | navegação ligada |
| `gaia.bearing` | rumo do mapa | número | graus | −180 a 180 | 50 | navegação ligada |
| `gaia.settled` | globo parado | número | — | 0 ou 1 | 50 | navegação ligada |

Tudo o que vem num mesmo pacote chega ao Pd no mesmo instante, um logo depois do outro e
sempre na mesma ordem:

- pacote de movimento: `gaia.acc.x`, `.y`, `.z`, `gaia.quat`, `gaia.mic.rms`,
  `gaia.mic.peak`, `gaia.breath` e, por último, `gaia.sensors`;
- pacote de navegação: `gaia.lat`, `gaia.lon`, `gaia.speed`, `gaia.bearing`,
  `gaia.settled`;
- pacote de CO₂: `gaia.co2` e `gaia.co2.trigger`.

### 6.2 Como receber

Um número: `[r gaia.co2]` ligado a uma caixa de número (*number box*, Ctrl+3).

Uma lista: `[r gaia.quat]` → `[unpack f f f f]`. O `[unpack]` separa a lista nas suas
partes, uma por saída, da esquerda para a direita. Para `gaia.sensors`, use
`[unpack f f f f f f f]` (sete `f`).

Para espiar no console, ligue o `[r ...]` a um `[print]`. Cuidado: a 50 vezes por segundo,
o console enche rápido.

O Pd escreve os números com ponto, à moda inglesa: o que este tutorial escreve como −22,9
aparece no Pd como `-22.9`.

### 6.3 Canal por canal

#### Aceleração: `gaia.acc.x`, `gaia.acc.y`, `gaia.acc.z`

**O que é.** O sensor sente duas coisas misturadas: a **gravidade**, que puxa sempre para
baixo com força de 1 g (1000 mg), e os **empurrões** que você dá na bola. *X*, *Y* e *Z*
são três direções perpendiculares presas à placa dentro da bola: frente-trás,
esquerda-direita e cima-baixo. Qual é qual depende de como a placa foi montada: descubra
com o exemplo 1.

**Valores.** Parada, a bola mostra perto de 1000 (ou −1000) no eixo que está na vertical e
perto de 0 nos outros dois. Inclinando devagar, a gravidade "passa" de um eixo para o
outro: só inclinando, cada eixo fica entre −1000 e 1000. Chacoalhando, os números pulam e
podem chegar a ±4000, o limite do sensor (4 g).

**Ideia musical.** A inclinação vira um controle contínuo: altura de um filtro, posição no
estéreo, afinação (exemplo 3). O chacoalhão vira intensidade: volume de um ruído, ou um
disparo quando passa de um limite (receitas [9.4](#94-separar-faixas-moses) e
[9.6](#96-medir-o-chacoalhão)).

#### Orientação: `gaia.quat`

**O que é.** Um **quatérnio**: quatro números (`w x y z`) que dizem para onde a bola está
virada em relação à posição de referência. Imagine um espeto atravessando a bola e um
giro em volta dele: `x y z` apontam a direção do espeto e `w` diz quanto ela girou. `w = 1`
quer dizer "nenhum giro" (a posição de referência vale `1 0 0 0`); `w` perto de 0,7 é um
quarto de volta (90°); `w` perto de 0 é meia volta (180°).

**Valores.** Cada número fica entre −1 e 1. Atenção: o quatérnio com todos os sinais
trocados (`−w −x −y −z`) descreve a mesma posição.

**A referência.** A bola usa o modo *Quat6*: giroscópio e acelerômetro, sem bússola
(magnetômetro). Por isso não há "norte": o zero é a posição em que a bola foi ligada, ou a
posição no momento em que você clica em **Zerar orientação** (config-app, painel Conexão,
com o PIN). O zero não é gravado: a cada vez que liga, a bola volta a usar a posição do
momento em que foi ligada. Atenção: se o site do GaiaSenses estiver conectado à bola, a
conexão dele também zera a bola (para todos, inclusive o Pd: `gaia.quat` e os ângulos de
`gaia.sensors` voltam ao neutro naquele instante) e o globo passa a começar no ponto que o
mapa do site mostra.

**Ideia musical.** Para a maioria dos usos, os ângulos de `gaia.sensors` ou a aceleração
são mais fáceis. Um truque simples com o quatérnio: `1 − |w|` (um menos o valor de `w` sem
o sinal) diz o quanto a bola saiu da posição de referência, para qualquer lado: 0 parada
no zero, crescendo à medida que ela gira.

#### Ângulos e mais: `gaia.sensors`

**O que é.** Sete números de uma vez: `roll pitch yaw` (ângulos, em graus), `aceX aceY
aceZ` (a mesma aceleração de `gaia.acc.*`, em mg) e `co2` (o último valor de `gaia.co2`;
fica 0 até chegar o primeiro pacote de CO₂). É a mesma lista que o site GaiaSenses manda ao
patch dele: um patch que só lê os valores roda igual nos dois.

**Roll, pitch e yaw** são nomes da aviação. Num avião: *roll* (rolagem) é inclinar as asas,
*pitch* (arfagem) é levantar ou abaixar o nariz e *yaw* (guinada) é virar o nariz para os
lados.

**Atenção: os nomes seguem a convenção do programa antigo da bola e do site**, e não a dos
livros. Comparando com os livros, roll e pitch estão trocados e o yaw tem o sinal
invertido. Tomando como guia o aviãozinho da vista 3D da config-app, na posição de
referência:

- o número chamado **roll** muda quando o nariz sobe ou desce;
- o chamado **pitch** muda quando as asas inclinam;
- o **yaw** muda quando o nariz vira para os lados, e virar para a esquerda dá valores
  negativos.

O jeito mais seguro é sempre o mesmo: mexa na bola e veja qual número muda (exemplo 1).

**Valores.** Roll e yaw vão de −180 a 180; pitch vai de −90 a 90. Na volta completa, o
valor salta de 180 para −180 (ou o contrário): cuidado ao ligar esses números direto numa
altura de nota.

**Ideia musical.** Roll e pitch como dois controles contínuos (filtro e estéreo, por
exemplo). Yaw como um parâmetro lento, cuidando do salto em ±180.

#### Microfones: `gaia.mic.rms` e `gaia.mic.peak`

**O que é.** Não é o som em si: é o **nível** do som, já medido na bola. Cada canal é uma
lista `E D`: microfone esquerdo e direito. O **RMS** é o nível médio num trecho curto (50
ms, no padrão); o **pico** é o ponto mais alto desse mesmo trecho. O pico reage a estalos;
o RMS acompanha melhor o "corpo" de um sopro ou de uma voz.

**Valores.** Em **dBFS**: 0 é o máximo que o microfone digital consegue registrar e −120 é
o piso (silêncio total). Quanto mais negativo, mais baixo. A cada 6 dB a menos, a amplitude
cai pela metade. Com o microfone saturando, o RMS pode passar um pouco de 0 (até uns +3).

**Quando chega.** Junto com cada pacote de movimento (50 vezes por segundo), desde que os
microfones tenham funcionado ao ligar a bola. Como viajam no pacote de movimento, também
precisam do sensor de movimento ligado ([seção 4, passo 5](#passo-5-só-na-primeira-vez-ligue-o-sensor-de-movimento)).
Desligar o "Detector de sopro" na config-app não para esses dois canais.

**Ideia musical.** Um "seguidor de envelope": quanto mais forte o sopro ou a voz perto da
bola, mais forte ou mais brilhante um som. A diferença entre E e D diz de que lado veio o
som.

#### Sopro: `gaia.breath`

**O que é.** 1 enquanto a bola reconhece um sopro, 0 no resto do tempo. Quem decide é a
própria bola. A sensibilidade vem do botão giratório da bola (de −60 a 0 dBFS) ou de um
valor fixo escolhido na config-app.

**Valores e tempo.** Vira 1 quando o som passa da linha de sensibilidade e volta a 0 quando
o som cai um pouco abaixo dela (6 dB, o "Rearme" da config-app). Depois de um sopro, a bola
só reconhece outro **3 segundos** depois (o "Debounce" da config-app). Chega 50 vezes por
segundo, repetindo o mesmo valor: para ter um único evento por sopro, use `[change]` e
`[sel 1]` ([receita 9.5](#95-transformar-o-sopro-num-disparo)). Vai a 0 na desconexão. Se o
**Detector de sopro** estiver desligado na config-app, fica sempre 0.

Cada sopro também dispara o pulso virtual de CO₂ (veja `gaia.co2`).

**Ideia musical.** Disparar notas, sons gravados, mudanças de seção (exemplo 2).

#### CO₂: `gaia.co2`

**O que é.** A quantidade de gás carbônico no ar, em **ppm** (partes por milhão): de cada
milhão de "pedacinhos" de ar, quantos são CO₂. Ao ar livre fica perto de 420; numa sala
cheia e fechada passa de 1000. O sensor mede de 400 a 5000 ppm.

**Quando chega.** O sensor dá uma leitura por segundo. A bola manda o valor quando ele
muda e, se não mudou, repete a cada segundo: cerca de **1 vez por segundo**. O sensor de
verdade é lento: leva minutos para acompanhar uma mudança no ar.

**Dois valores especiais:**

- **2100 por 10 segundos, logo depois de um sopro: o "pulso virtual".** Como o sensor real é
  lento demais para um instrumento, a bola "finge" um salto de CO₂ no instante do sopro,
  para a música reagir na hora. É um degrau: do valor real direto para 2100, parado ali por
  10 segundos, e de volta à leitura real. Um sopro durante o pulso não o reinicia. O valor
  e a duração se mudam na config-app (Configuração → CO₂: **Pico virtual (ppm)** e
  **Duração virtual (s)**, com o PIN); duração 0 desliga o pulso.
- **600 o tempo todo: o sensor de CO₂ está desligado** na config-app (**Sensor de CO₂
  (SC8)**). A bola manda 600 no lugar da leitura, para os patches terem um valor
  plausível. O pulso virtual continua funcionando.

Na config-app, o cartão do CO₂ (painel "Sensores em tempo real") diz de onde vem o número:
leitura real, pulso virtual, sem leitura ou inválido.

**Ideia musical.** Um parâmetro muito lento: o volume de um drone, o andamento de uma peça
longa (exemplo 4). Ou aproveite o pulso como um "evento de sopro" de 10 segundos.

#### Gatilho de CO₂: `gaia.co2.trigger`

**O que é.** Vira 1 quando o CO₂ passa de **1200 ppm** e só volta a 0 quando cai a **1150
ou menos**. Essa folga de 50 ppm é a **histerese**: evita que o gatilho fique piscando
quando o valor oscila perto de 1200, como um termostato que não liga e desliga sem parar.

É calculado sobre o valor que a bola manda, **incluindo o pulso virtual**: no padrão, todo
sopro (2100 é mais que 1200) liga o gatilho por uns 10 segundos. É o mesmo gatilho que
abre as composições no site GaiaSenses. Se o sensor parar de responder, o gatilho vai a 0
(menos durante o pulso).

Chega com cada pacote de CO₂; uma mudança de estado é enviada na hora. Vai a 0 na
desconexão. O nível e a histerese se mudam na config-app (**Gatilho de CO₂ (ppm)** e
**Histerese do gatilho (ppm)**).

**Ideia musical.** `[change]` → `[sel 1]` para começar uma seção quando a sala "enche" (ou
quando alguém sopra).

#### Navegação: `gaia.lat`, `gaia.lon`, `gaia.speed`, `gaia.bearing`, `gaia.settled`

**O que é.** O globo virtual que a bola calcula. O modo se escolhe na config-app
(Configuração → Navegação do globo → **Modo de navegação**, com o PIN):

- **Espelho** (o padrão): o globo é a própria bola. Girou a bola 30°, o globo girou 30°.
- **Joystick:** inclinar a bola faz o globo girar sem parar naquela direção, mais rápido
  quanto mais inclinada; de volta à posição de referência, ele para.
- **Desligado:** estes cinco canais não chegam.

**Os cinco canais:**

- `gaia.lat` e `gaia.lon`: o ponto que fica no centro da tela, em graus. Latitude de −90
  (polo sul) a 90 (polo norte); longitude de −180 a 180 (oeste negativo). Na posição de
  referência, o globo mostra **Campinas (SP)**: `gaia.lat` perto de −22,9 e `gaia.lon`
  perto de −47,06. Esse ponto de referência se muda na config-app. Se o site do
  GaiaSenses se conectou à bola, `gaia.lat` e `gaia.lon` passam a começar no ponto que o
  mapa do site mostrava naquele momento, e não em Campinas, até a bola reiniciar ou alguém
  mudar o ponto de referência.
- `gaia.speed`: a velocidade do giro do globo, em graus por segundo. No Espelho, é a
  velocidade medida (sem teto fixo). No Joystick, vai de 0 até a velocidade máxima
  configurada (padrão 90, no máximo 720) e fica 0 com o globo parado.
- `gaia.bearing`: o **rumo** do mapa, de −180 a 180 graus: para que lado fica o norte na
  tela, medido no sentido horário. 0 = norte para cima.
- `gaia.settled`: 1 quando o globo é considerado parado. A bola espera o globo ficar
  devagar por cerca de 0,4 segundo antes de dizer que parou, então o valor não fica
  piscando.

**Quando chega.** No ritmo da navegação (padrão 50 vezes por segundo, nunca mais que o
movimento), só com a navegação ligada e o sensor de movimento funcionando. Com a navegação
desligada, a bola manda uma vez por segundo um pacote "vazio", que o objeto ignora (ele só
aparece na contagem do `rate`).

**Ideia musical.** Latitude como altura de nota, longitude como posição no estéreo,
velocidade como intensidade ou andamento, `gaia.settled` como um sino quando o globo para
(exemplo 5).

## 7. Taxas de atualização e o que isso significa para o som

**Hz** (hertz) quer dizer "vezes por segundo".

| Origem | Vezes por segundo | Onde se muda |
|---|---|---|
| Movimento (com microfones e sopro junto) | 50 no padrão; de 10 a 200 | config-app: Configuração → Básico → **Taxa (Hz)** (grupo Aquisição) |
| Navegação | 50 no padrão; nunca mais que o movimento | config-app: Configuração → Avançado → Navegação do globo → **Taxa de envio (Hz)** |
| CO₂ | cerca de 1 | não se muda: é o ritmo do sensor |
| **Total (`rate`)** | **cerca de 100** (50 + 50 + 1) | — |

Os outros pacotes da bola (diagnóstico, gráficos da config-app) não vão ao Pd.

O que isso quer dizer na prática:

- **50 vezes por segundo é um valor novo a cada 20 ms.** O cinema mostra 24 imagens por
  segundo; 50 é mais que o dobro. É rápido o bastante para controle contínuo: filtros,
  volume, glissandos.
- **Mas os valores chegam em degraus.** Ligados direto num parâmetro de áudio (a frequência
  de um `[osc~]`, um volume), os degraus podem soar como um chiado ou "serrilhado". A
  solução é suavizar com `[line~]`, numa rampa de 20 a 50 ms ([receita 9.2](#92-suavizar-line-e-line)).
- **O CO₂ muda devagar** (minutos). Use rampas longas, de 1 a 2 segundos ou mais. O pulso
  virtual, ao contrário, é um degrau instantâneo: suavize se quiser uma subida gradual.
- **Atraso.** Se o Pd não acompanha, a config-app descarta o excesso em vez de acumular
  atraso. O modo palco (próxima seção) deixa o caminho mais curto.

## 8. Modo palco e modo ajuste

A chave **Modo ajuste ⟷ Modo palco** fica no alto da config-app. O Pd recebe os mesmos
dados nos dois modos.

- **Modo ajuste:** tudo funciona (vista 3D, gráficos, configuração). Use para preparar e
  testar.
- **Modo palco:** para apresentar. Os dados vão ao Pd com o menor atraso possível; a vista
  3D, os gráficos e os ajustes ficam pausados. Continuam ativos:
  - o estado do Pd ("Pd: conectado · N pacotes/s");
  - o medidor do sopro (o nível do microfone mais alto, de −90 a 0 dBFS) e a luz do pulso
    de sopro;
  - o campo **Sensibilidade do sopro** (em dBFS, de −90 a 0). Se a sensibilidade estiver
    no botão giratório da bola, o campo avisa "controlado pelo potenciômetro". Esse campo
    escreve na bola, então precisa do PIN: desbloqueie **antes** de entrar no modo palco
    (ou saia, desbloqueie e volte);
  - os botões **Desconectar** e **Sair do modo palco**.

O navegador lembra o modo escolhido. Dica: ensaie em modo palco, para ter as mesmas
condições da apresentação.

## 9. Receitas

Nos desenhos abaixo, `[ ]` é um objeto, `[ (` é uma caixa de mensagem e `|` é uma ligação
de cima para baixo.

### 9.1 Escalar: levar um valor para a faixa que você quer

A receita geral tem dois passos: levar o valor para **0 a 1** e depois para a faixa nova.

```
[r gaia.acc.x]
|
[clip -1000 1000]    corta o que passar da faixa (um chacoalhão)
|
[+ 1000]             agora vai de 0 a 2000
|
[/ 2000]             agora vai de 0 a 1
|
[* 600]              agora vai de 0 a 600
|
[+ 200]              agora vai de 200 a 800 (por exemplo, em Hz)
```

A mesma conta numa caixa só, com `[expr]` (`$f1` é "o número que entrou"):

```
[expr 200 + ($f1 + 1000) / 2000 * 600]
```

Comece sempre com `[clip]`: ele garante que nada sai da faixa, mesmo num chacoalhão.

Para altura de nota, prefira escalar para **notas MIDI** e converter com `[mtof]`: passos
iguais em notas soam mais naturais que passos iguais em Hz. Exemplo: de 0 a 1 →
`[* 72]` → `[+ 36]` → `[mtof]` cobre de um dó grave (36) a um dó bem agudo (108).

### 9.2 Suavizar: `[line]` e `[line~]`

Para **som** (volume, frequência de filtro, frequência de oscilador):

```
[r gaia.acc.x]
|
...                  (escale como em 9.1)
|
[$1 30(              "vá até o número novo em 30 ms"
|
[line~]              desliza suavemente, sem degraus
|
[*~]  ou  [vcf~]  ou  [osc~]
```

Na caixa de mensagem `[$1 30(`, o `$1` é trocado pelo número que chega, e 30 é o tempo da
rampa em milissegundos. Para os canais de movimento (a cada 20 ms), uma rampa de 20 a 50
ms funciona bem. Para o CO₂, use algo como `[$1 2000(`. O `[line~]` só funciona com o som
do Pd ligado (DSP).

Para **números de controle** (que vão para outros objetos, não para o som), use `[line]`
sem o til: `[$1 30(` → `[line]`. Ele solta números intermediários (um a cada 20 ms, no
padrão).

### 9.3 Detectar mudanças: `[change]`

Os canais de 0 ou 1 (`gaia.breath`, `gaia.settled`, `gaia.co2.trigger`) chegam muitas
vezes por segundo repetindo o mesmo valor. O `[change]` só deixa passar quando o valor
muda:

```
[r gaia.settled]
|
[change]             sai 1 quando o globo para e 0 quando volta a girar
```

Também serve depois de um `[int]`, para só tocar uma nota quando ela muda de verdade
(exemplo 5).

### 9.4 Separar faixas: `[moses]`

O `[moses 1000]` manda pela saída da esquerda os números **menores** que 1000, e pela da
direita os números de **1000 para cima**:

```
[r gaia.co2]
|
[moses 1000]
|          \
abaixo     1000 ou mais
de 1000
```

Exemplos: CO₂ abaixo de 1000 toca um som e acima toca outro; `[r gaia.acc.z]` →
`[moses 0]` diz se aquele eixo aponta para cima ou para baixo; um chacoalhão acima de 2000
dispara um golpe. Para três faixas, ponha um segundo `[moses]` numa das saídas.

### 9.5 Transformar o sopro num disparo

```
[r gaia.breath]      chega 50 vezes por segundo: 0 0 0 1 1 1 1 0 0 ...
|
[change]             só as mudanças: 1 (começou) e 0 (terminou)
|
[sel 1]              um bang quando o sopro começa
|
[bng]                ligue aqui a sua nota, o seu som gravado...
```

Sem o `[change]`, o `[sel 1]` dispararia 50 vezes por segundo enquanto o sopro dura. Para
saber também quando o sopro **termina**, use `[sel 1 0]`: a primeira saída dá o bang do
começo e a segunda, o do fim. O exemplo 2 usa esta receita.

### 9.6 Medir o chacoalhão

O tamanho total da aceleração, juntando os três eixos, não depende de como a bola está
virada: parada, dá perto de 1000 (só a gravidade); chacoalhando, passa disso.

```
[r gaia.sensors]
|
[unpack f f f f f f f]
            (use a 4ª, a 5ª e a 6ª saída: aceX, aceY, aceZ)
|
[expr sqrt($f1*$f1 + $f2*$f2 + $f3*$f3)]
|
[- 1000]             perto de 0 parada; cresce com o chacoalhão
```

Ligue a 4ª saída do `[unpack]` na 1ª entrada do `[expr]`, a 5ª na 2ª e a 6ª na 3ª. O
`[unpack]` solta os valores da direita para a esquerda, então o `[expr]` calcula uma vez
só, com os três valores novos, quando chega o `aceX` na entrada da esquerda.

## 10. Exemplos prontos

Os exemplos ficam na pasta [`exemplos/`](exemplos/), ao lado deste tutorial. Se você
instalou pelo Deken, os exemplos e este tutorial não vêm junto: baixe-os da pasta
[`pd-gaiaball`](https://github.com/GaiaSenses/gaiaball/tree/main/pd-gaiaball) do
repositório.

Para abrir: no Pd, **Arquivo → Abrir** e escolha o arquivo `.pd`. Cada exemplo tem o seu
`[gaiaball]`; com vários abertos, os de depois avisam "porta em uso" no console, e tudo
continua funcionando ([seção 5](#um-gaiaball-por-porta)). Os exemplos que fazem som
pedem para ligar o som do Pd: clique na mensagem `dsp 1` (ou Ctrl+/). Comece com o volume
do computador baixo.

### 10.1 `01-ver-tudo.pd`: ver todos os dados

**O que faz.** Mostra, em caixas de número e quadradinhos, todos os canais e o estado da
conexão. Não faz som.

**O que experimentar.** Incline a bola devagar e veja qual eixo de `gaia.acc` muda;
descubra qual aponta para cima com a bola parada. Gire a bola e acompanhe `roll`, `pitch`,
`yaw`, `gaia.lat` e `gaia.lon`. Sopre: o quadrado do sopro acende, e o CO₂ salta para 2100
por 10 segundos.

### 10.2 `02-sopro-toca-nota.pd`: o sopro toca uma nota

**O que faz.** Cada sopro toca uma nota (`[osc~]`) com ataque rápido e final suave, feito
com `[vline~]`. Tem controle de volume e um botão de teste para tocar sem a bola.

**O que experimentar.** Ligue o som (`dsp 1`) e sopre na bola. Arraste o número da nota
(60 = dó central). Mude os números do envelope `1 10, 0 1500 10` (sobe até 1 em 10 ms e
desce até 0 em 1500 ms). Lembre: a bola espera 3 segundos entre um sopro e outro. No modo
demo, um sopro chega a cada 6 segundos.

### 10.3 `03-inclinacao-filtro.pd`: inclinar a bola mexe num filtro

**O que faz.** Um ruído passa por um filtro `[vcf~]`. `gaia.acc.x` escolhe a altura do
filtro (escalada para notas MIDI de 36 a 108 e convertida com `[mtof]`), e `gaia.acc.y`
escolhe a ressonância (de 1 a 20). As mudanças são suavizadas com `[line~]`.

**O que experimentar.** Incline a bola para os lados e para frente e para trás. Troque a
faixa nas contas para um efeito mais sutil. Troque `[vcf~]` por `[lop~]`. No modo demo, o
eixo x oscila devagar entre −250 e 250 mg.

### 10.4 `04-co2-volume.pd`: o CO₂ controla o volume de um drone

**O que faz.** Três osciladores formam um drone (um som contínuo). `gaia.co2` de 400 a
2000 ppm vira o volume de 0 a 1, com uma rampa lenta de 2 segundos.

**O que experimentar.** Sopre: o pulso virtual leva o CO₂ a 2100 e o drone sobe ao máximo
por 10 segundos. Numa sala, o CO₂ real sobe devagar com as pessoas respirando. Ajuste os
números do `[clip]` e das contas para a sua sala. Se o valor ficar parado em 600, o sensor
de CO₂ está desligado na config-app.

### 10.5 `05-globo-altura.pd`: o globo vira música

**O que faz.** A latitude escolhe a nota (de 48 a 96, arredondada para semitons com
`[int]`), a longitude leva o som da esquerda para a direita e, quando o globo para
(`gaia.settled`), toca um sino.

**O que experimentar.** Gire a bola devagar para o norte e para o sul (a nota muda) e para
leste e oeste (o som passeia no estéreo). Pare a bola: o sino toca. Na linha de data (180°
de longitude), o som pula de um lado para o outro. Precisa da navegação ligada (Espelho é
o padrão). No modo demo, o globo passeia sozinho e para a cada 10 segundos.

### 10.6 `06-estado-da-conexao.pd`: o estado da conexão

**O que faz.** Usa a saída do `[gaiaball]` para mostrar se está conectado, o `rate` e o
`gap`, com avisos: "poucos pacotes" quando o `rate` fica abaixo de 10, e "ATENÇÃO: buraco
de N ms" quando o `gap` passa de 200 ms.

**O que experimentar.** Desligue a chave **Enviar ao Pd** na config-app: aparece
"desconectado". Ligue de novo. Com a bola no padrão, o `rate` fica perto de 100; no modo
demo, perto de 71. Um `rate` perto de 2 quer dizer que o sensor de movimento está desligado
([seção 11](#só-chegam-gaiaco2-e-gaiaco2trigger-e-o-rate-fica-perto-de-2)). O limite de 200
ms é uma escolha: mude o número do `[moses]`.

## 11. Problemas comuns

### O Pd diz `gaiaball ... couldn't create` (a caixa fica tracejada)

O objeto não está instalado, ou a pasta dele não está no caminho do Pd. Volte à
[seção 3](#3-instalação-passo-a-passo) e confira **Preferências → Caminho**.

### Nada chega e a config-app mostra "Pd: aguardando"

A config-app procura o Pd e não acha. Confira: o Pd está aberto? Há um patch aberto com
`[gaiaball]`? A porta é a mesma nos dois lados (`[gaiaball]` = 9478; painel Conexão → Pd →
Porta)? O console mostra `porta inválida`?

### A config-app mostra "Pd: bloqueado pelo Chrome"

A permissão "Apps neste dispositivo" foi negada. Libere como no
[passo 7](#passo-7-libere-o-chrome-uma-vez) e recarregue a página.

### A config-app mostra "Pd: desligado" ou "Pd: aguardando a bola"

"Desligado": a chave **Enviar ao Pd** está desligada. "Aguardando a bola": a chave está
ligada, mas a bola não está conectada; clique em **Conectar**.

### O console diz `gaiaball: porta 9478 em uso`

Outro `[gaiaball]` já atende nessa porta (num outro patch aberto ou no patch de ajuda).
Não atrapalha: os dados chegam do mesmo jeito, e este assume a porta quando o outro for
fechado. Se a porta estiver ocupada por **outro programa**, escolha outra porta nos dois
lados, por exemplo `[gaiaball 9500]` e 9500 na config-app.

### Aparece `connected 0` sozinho

A config-app fechou a conexão (a aba foi fechada ou recarregada, a chave foi desligada) ou
ficou 2 segundos sem mandar nada (a bola desconectou, ficou longe demais, ou a página
travou). Quando os pacotes voltam, chega `connected 1` sozinho.

### Só chegam `gaia.co2` e `gaia.co2.trigger`, e o `rate` fica perto de 2

**O sensor de movimento (IMU) está desligado**, como vem de fábrica. Sem ele, a bola não
manda movimento, microfones, sopro nem globo; só o CO₂ (uma vez por segundo) e um pacote
vazio de navegação (uma vez por segundo). Ligue o IMU como no
[passo 5](#passo-5-só-na-primeira-vez-ligue-o-sensor-de-movimento). Se o **Estado do IMU**
disser `selecionado, não encontrado`, o modelo escolhido não é o da bola.

### `connected 1` e `rate` perto de 100, mas nenhum `gaia.*` chega

A bola está com um programa (firmware) antigo, cujo pacote de movimento o objeto não
entende. A config-app mostra o aviso *"Firmware da bola desatualizado"*. É preciso gravar o
firmware novo na bola: fale com a equipe.

### `gaia.lat`, `gaia.lon`, `gaia.speed`, `gaia.bearing` e `gaia.settled` não chegam

A navegação está desligada na bola (com isso, o `rate` fica perto de 52). Na config-app,
desbloqueie com o PIN e escolha **Espelho** ou **Joystick** em **Modo de navegação**. A
navegação também precisa do sensor de movimento ligado.

### `gaia.mic.rms` e `gaia.mic.peak` não chegam

Os microfones não funcionaram quando a bola ligou (a bola deixa de mandar esses dois
canais), ou o sensor de movimento está desligado (eles viajam no pacote de movimento).
Desligar o "Detector de sopro" não para esses canais. Confira os cartões dos microfones no
painel "Sensores em tempo real" da config-app.

### `gaia.breath` nunca vira 1

O **Detector de sopro** está desligado na config-app, ou a sensibilidade está alta demais
para a sala: gire o botão da bola, ou ajuste o campo de sensibilidade. Lembre também que a
bola espera 3 segundos entre um sopro e outro.

### `gaia.co2` fica em 600 o tempo todo

O sensor de CO₂ está desligado na config-app (**Sensor de CO₂ (SC8)**). Ligue-o com o PIN.

### `gaia.co2` vai a 2100 e fica 10 segundos

Não é defeito: é o pulso virtual do sopro (veja [CO₂](#co₂-gaiaco2)). Para desligar,
ponha 0 em **Duração virtual (s)**. Para que ele não ligue o gatilho, ponha o **Pico
virtual** abaixo do **Gatilho de CO₂**.

### `rate` baixo ou `gap` alto

A página da config-app ficou lenta. Feche abas pesadas, deixe só uma aba da config-app
enviando e use o modo palco.

### Os ângulos parecem tortos, ou o zero está no lugar errado

O zero é a posição em que a bola foi ligada. Com a bola parada na posição que você quer
como zero, clique em **Zerar orientação** (painel Conexão, com o PIN). Se o site do
GaiaSenses estiver conectado à bola, a conexão dele também zera a bola, na posição em que
ela estava naquele instante: zere de novo depois que o site conectar. Para os nomes
trocados de roll e pitch, veja [Ângulos e mais](#ângulos-e-mais-gaiasensors).

### Abri duas abas da config-app

Vale a conexão mais recente. Feche as outras.

## 12. Glossário

**127.0.0.1** — o endereço que quer dizer "este computador". O `[gaiaball]` só aceita
conexões vindas daí.

**Bang** — no Pd, um sinal de "agora!", sem número. O `[bng]` (o botão redondo) mostra e
manda bangs.

**Bluetooth** — a conexão sem fio entre a bola e o computador.

**config-app** — a página da Gaiaball que roda no Chrome ou no Edge: conecta a bola,
configura e repassa os dados ao Pd.

**dBFS** — decibéis em relação ao máximo que um sistema digital registra ("FS" = *full
scale*, fundo de escala). 0 dBFS é o máximo; números negativos são mais baixos; −120 é o
piso da escala. A cada 6 dB a menos, a amplitude cai pela metade; 20 dB a menos é um
décimo.

**Deken** — a "loja" de objetos extras dentro do Pd (Ajuda → Encontrar externals).

**DSP** — o motor de som do Pd. Desligado, nenhum objeto com til (`~`) faz som. Liga com
a mensagem `; pd dsp 1` ou Ctrl+/.

**External** — um objeto extra, que não vem com o Pd. O `[gaiaball]` é um.

**g** — a força da gravidade: o "peso" que você sente parado. 1 g = 1000 mg.

**Histerese** — uma folga entre o ponto de ligar e o de desligar, para não ficar piscando
(como um termostato). No gatilho de CO₂: liga acima de 1200, desliga em 1150 ou menos.

**Hz (hertz)** — vezes por segundo. 50 Hz = 50 vezes por segundo = uma vez a cada 20 ms.
Em som, também é a frequência de uma nota (o lá central tem 440 Hz).

**IMU** — o sensor de movimento da bola (acelerômetro e giroscópio, e um magnetômetro que o
modo padrão não usa). Vem desligado de fábrica.

**mg (miligê)** — um milésimo de g. Não é miligrama! 1000 mg = 1 g = a gravidade.

**MIDI (nota)** — um número para cada nota do piano: 60 é o dó central, e cada número a
mais é um semitom acima. `[mtof]` converte a nota em frequência (Hz).

**ms** — milissegundo, um milésimo de segundo.

**Patch** — um programa do Pd, feito de caixas ligadas por fios.

**PIN** — a senha de 4 números da bola, que protege a configuração. De fábrica: `1234`.

**Porta** — o número que identifica um programa dentro do computador (o "apartamento" no
"prédio" 127.0.0.1). O `[gaiaball]` usa a 9478, se você não disser outra.

**ppm (partes por milhão)** — de cada milhão de partes de ar, quantas são de CO₂. Ar livre:
perto de 420. Sala cheia e fechada: mais de 1000.

**Quatérnio** (*quaternion*, em inglês) — quatro números (`w x y z`) que descrevem uma
rotação: um eixo (`x y z`) e quanto se girou em volta dele (`w`). `1 0 0 0` = nenhum giro.

**RMS e pico** — duas medidas de nível de som. O RMS é uma média (o "corpo" do som); o pico
é o ponto mais alto (os estalos).

**Roll, pitch e yaw** — ângulos da aviação: rolagem (inclinar as asas), arfagem (nariz para
cima e para baixo) e guinada (nariz para os lados). Em `gaia.sensors`, seguem a convenção
antiga do site, com roll e pitch trocados: veja [Ângulos e mais](#ângulos-e-mais-gaiasensors).

**Sinal (objetos com `~`)** — objetos que trabalham com som (`[osc~]`, `[line~]`,
`[dac~]`). Os sem til trabalham com números de controle.

**WebSocket** — um tipo de conexão por onde uma página da web troca dados com outro
programa. Aqui, é o "cano" entre a config-app e o `[gaiaball]`, que não sai do seu
computador.
