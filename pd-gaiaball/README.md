# pd-gaiaball — objeto `[gaiaball]` para Pure Data

Os dados da Gaiaball dentro do Pure Data, no Windows, no Linux e no macOS. Quem conversa
com a bola por Bluetooth é a **config-app**, aberta no Chrome ou no Edge. Ela repassa os
pacotes ao objeto `[gaiaball]`, que publica cada valor num nome: no seu patch, basta um
`[r gaia.lat]`, `[r gaia.co2]`, `[r gaia.breath]`… Não há terminal, Python nem programa
extra: só o Pd e o navegador. Nada é escrito na bola; o objeto só escuta.

O `[gaiaball]` é uma **abstração**: um patch (`gaiaball.pd`) feito só com objetos que já vêm
com o Pd vanilla 0.51 ou mais novo. Não há nada para compilar, e o mesmo arquivo funciona em
qualquer sistema e processador.

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

### 1. Instalar o `[gaiaball]` (uma vez)

**Pelo Deken**, quando a versão estiver lá: no Pd, **Ajuda → Encontrar externals** (no Pd
em inglês: *Help → Find externals*), procure `gaiaball` e clique em **Instalar**. O Deken
põe a pasta `gaiaball` numa pasta que o Pd já procura; se ele perguntar se deve acrescentar
a pasta ao caminho de busca, tanto faz.

**Pelo pacote da página de versões**
(<https://github.com/GaiaSenses/gaiaball/releases>): baixe `pd-gaiaball-<versão>.zip`, que
serve para todos os sistemas, e copie a pasta `gaiaball` que vem dentro dele para a pasta
de objetos que o Pd procura sozinho:

| Sistema | Copie a pasta `gaiaball` para | O objeto fica em |
|---|---|---|
| Windows | `%AppData%\Pd` | `C:\Users\<você>\AppData\Roaming\Pd\gaiaball\gaiaball.pd` |
| macOS | `~/Library/Pd` (no Finder, **Biblioteca**) | `~/Library/Pd/gaiaball/gaiaball.pd` |
| Linux | `~/.local/lib/pd/extra` (ou `~/pd-externals`) | `~/.local/lib/pd/extra/gaiaball/gaiaball.pd` |

Crie a pasta `Pd` (ou `extra`) se ela não existir. Não é preciso mexer em **Preferências →
Caminho**: essas pastas são os "caminhos padrão" do Pd, e o Pd acha `gaiaball/gaiaball.pd`
dentro delas sozinho. O `LEIA-ME.html` do pacote mostra cada passo, sistema por sistema.

**Direto do repositório**: copie a pasta `pd-gaiaball` inteira (ou só o `gaiaball.pd` e o
`gaiaball-help.pd`) para a pasta do seu patch, ou ponha a pasta em **Preferências →
Caminho**.

> **Tinha o `[gaiaball]` antigo, compilado?** Apague antes os arquivos `gaiaball.dll`,
> `gaiaball.pd_darwin`, `gaiaball.pd_linux` e `gaiaball.*.so`, inclusive os que estiverem
> na pasta dos seus patches. Se um deles ficar ao lado do `gaiaball.pd` ou mais acima no
> caminho de busca, o Pd carrega o binário antigo no lugar da abstração. O antigo se
> denuncia escrevendo `gaiaball: aguardando a config-app em ws://...` no console.

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
funcionando e tem um botão que abre a config-app. Fora do modo de edição, um clique no
`[gaiaball]` abre a janela dele, com o estado da conexão e o botão **reconectar**.

- `[gaiaball]` escuta na porta 9478, a padrão da config-app.
- `[gaiaball 9500]` escuta na porta 9500. Nesse caso, mude também a porta na config-app
  (painel Conexão → Pd → Porta). Use um número inteiro de 1 a 65535.
- **Só um `[gaiaball]` por porta escuta.** O segundo fica parado, e o console pode mostrar
  `netreceive: listen failed: Address already in use` (no Linux e no macOS; o Windows pode
  não avisar). Enquanto o primeiro estiver aberto, os dados chegam do mesmo jeito. O
  segundo **não** assume a porta sozinho quando o primeiro é fechado: abra o que ficou
  (clique nele) e clique em **reconectar**.
- **O patch de ajuda não ocupa a porta.** Ele só cria um `[gaiaball]` próprio quando você
  liga a chave **TESTAR DAQUI**, e o apaga quando você a desliga ou fecha a ajuda.

### 4. Liberar o Chrome (uma vez)

Na primeira conexão, o Chrome pergunta se o site pode acessar **Apps neste dispositivo**.
Clique em **Permitir**. Sem essa permissão, a página não consegue falar com o Pd, e o
indicador no alto da config-app mostra "Pd: bloqueado pelo Chrome".

Se você bloqueou sem querer: clique no ícone à esquerda do endereço → **Configurações do
site** → **Apps neste dispositivo** → **Permitir**, e recarregue a página.

Pronto: o indicador mostra "Pd: conectado · N pacotes/s", e a saída do `[gaiaball]` manda
`connected 1`.

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

## Saída de status

A saída do `[gaiaball]` manda estas mensagens. As mesmas chegam em `[r gaiaball.status]`,
de qualquer `[gaiaball]` aberto.

| Mensagem | Quando |
|---|---|
| `connected 1` | a config-app conectou, ou os pacotes voltaram depois de um silêncio |
| `connected 0` | a config-app fechou a conexão, ou ficou 2 segundos sem mandar pacotes |
| `rate N` | uma vez por segundo, enquanto conectado: pacotes recebidos no último segundo (movimento + navegação + CO₂; com a bola no padrão, cerca de 100) |
| `gap N` | junto com o `rate`: o maior intervalo sem pacotes no último segundo, em milissegundos |

**Uma conexão de cada vez.** O `[gaiaball]` atende um cliente por vez: se um segundo
conectar, os dois caem, e a config-app reconecta sozinha em 2 s. Por isso deixe só uma aba
da config-app com **Enviar ao Pd** ligada: com duas, elas se derrubam.

## Solução de problemas

| Sintoma | Causa provável e o que fazer |
|---|---|
| O Pd diz `gaiaball ... couldn't create` | o Pd não achou o `gaiaball.pd`. Confira a pasta (tabela do passo 1, sem uma pasta a mais no meio) e, em Preferências → Caminho, que **Usar caminhos padrão** está ligado. |
| O console diz `gaiaball: aguardando a config-app em ws://...` | é o `[gaiaball]` antigo, compilado. Apague os binários antigos (passo 1). |
| O console diz `netreceive: listen failed: Address already in use` | outro `[gaiaball]` (em outro patch aberto) já escuta nessa porta. Não atrapalha enquanto ele estiver aberto. Se você fechar aquele patch, clique no `[gaiaball]` que ficou e em **reconectar**. Se a porta estiver com outro programa, use outra porta nos dois lados: `[gaiaball 9500]` e a mesma porta na config-app. |
| A config-app mostra "Pd: aguardando" | o Pd não está aberto, não há `[gaiaball]` escutando em nenhum patch aberto (veja a linha anterior), ou as portas são diferentes nos dois lados. |
| A config-app mostra "Pd: bloqueado pelo Chrome" | a permissão "Apps neste dispositivo" foi negada. Libere como no passo 4 e recarregue a página. |
| A config-app mostra "Pd: desligado" | a chave Enviar ao Pd está desligada. |
| A config-app mostra "Pd: aguardando a bola" | a chave está ligada, mas a bola ainda não foi conectada na config-app: clique em **Conectar**. |
| `connected 1` e `connected 0` se alternando | duas abas (ou dois navegadores) com Enviar ao Pd ligado: uma derruba a outra. Deixe só uma. |
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
objeto. Uma página assim pode mandar dados falsos ou cortar os dados de verdade: basta ela
abrir uma segunda conexão para as duas caírem. Por isso, durante uma apresentação, feche as
páginas que você não conhece.

## Para quem mantém o projeto

### Conteúdo da pasta

| Arquivo | O que é |
|---|---|
| `gaiaball.pd` | a abstração. **Gerada**: não edite à mão |
| `tools/gerar_patch.py` | o gerador do `gaiaball.pd` (Python 3, sem dependências) |
| `gaiaball-help.pd` | patch de ajuda (botão direito no objeto → Ajuda) |
| `exemplos/` | os seis patches do tutorial |
| `TUTORIAL.md` | o passo a passo para músicos |
| `tests/e2e.mjs`, `tests/imprime.pd`, `tests/objeto.pd` | teste ponta a ponta num Pd de verdade |
| `tests/ajuda.mjs` | teste do patch de ajuda (não pode ocupar a porta ao abrir) |
| `pacotes/` | o que monta o pacote da página de versões e o do Deken (`montar.py`, `LEIA-ME.md`, `NOTAS-DA-VERSAO.md`, `estilo.css`, `filtro.lua`) |
| [`PUBLICAR-NO-GITHUB.md`](PUBLICAR-NO-GITHUB.md) | como publicar uma versão na página de versões do GitHub |
| [`PUBLICAR-NO-DEKEN.md`](PUBLICAR-NO-DEKEN.md) | como enviar a versão ao Deken |

### Como o `gaiaball.pd` funciona

- `[pd rede]`: `[netreceive -b]` escuta só em 127.0.0.1, na porta do argumento (padrão
  9478). Atende um cliente por vez: se um segundo conectar, os dois caem e a config-app
  reconecta sozinha em 2 s. O botão **reconectar** manda `listen` de novo.
- `[pd http]` + `[pd sha1]`: o aperto de mão do WebSocket (RFC 6455). O SHA-1 e o base64
  são calculados com `[expr]`. Como o Pd guarda os números em float de 32 bits (inteiros
  exatos só até 2²⁴), cada palavra de 32 bits vira duas metades de 16 bits. Roda uma vez
  por conexão.
- `[pd websocket]`: lê os quadros byte a byte, numa máquina de estados dentro de um único
  `[expr]`. Também responde a ping e a close, e derruba a conexão num quadro sem máscara.
- `[pd pacotes]`: decodifica os pacotes `0x01` (movimento, v2), `0x02` (CO₂) e `0x07`
  (navegação) como diz [`firmware/docs/protocol.md`](../firmware/docs/protocol.md). Bytes a
  mais (campos futuros) são ignorados; pacotes curtos ou de outra versão são descartados.
- `[pd estado]`: `connected`, `rate` e `gap`, na saída e em `[r gaiaball.status]`.

O `[netreceive]` não avisa o patch quando a porta não abre, e por isso o `[gaiaball]` não
consegue esperar a porta ficar livre e assumi-la, como o antigo objeto compilado fazia.
Daí o patch de ajuda só criar o seu servidor sob demanda.

**Para mudar o patch**, edite `tools/gerar_patch.py` e rode:

```sh
python3 pd-gaiaball/tools/gerar_patch.py
```

Faça o commit do gerador e do `gaiaball.pd` juntos. O CI roda o gerador de novo e falha se
o resultado for diferente do `gaiaball.pd` do commit, mesmo que num byte só.

### Testes

```sh
node pd-gaiaball/tests/e2e.mjs --pd /caminho/do/pd             # gaiaball.pd aberto como patch
node pd-gaiaball/tests/e2e.mjs --pd /caminho/do/pd --objeto    # [gaiaball] criado como objeto
node pd-gaiaball/tests/ajuda.mjs --pd /caminho/do/pd           # a ajuda não ocupa a porta
node pd-gaiaball/tests/ajuda.mjs --pd /caminho/do/pd --com-objeto
```

O `e2e.mjs` abre o Pd sem interface (`-nogui -noprefs`) e faz o aperto de mão como o
Chrome. Ele confere:

- a chave da RFC 6455 e mais 40 chaves aleatórias, contra o `node:crypto`;
- os pacotes de referência do protocolo e os ângulos de Euler, contra a convenção do site
  (`config-app/src/protocol/euler.ts`);
- pedido HTTP e quadros partidos em vários pedaços, quadro com tamanho de 16 bits;
- ping, close, quadro sem máscara, pedido sem chave e dois clientes ao mesmo tempo;
- silêncio de 2 s e o ritmo de 100 pacotes/s;
- com `--objeto`: o `[gaiaball]` achado pelo caminho de busca e as mensagens da saída.
  Com `--objeto --instalado`, sem nenhum `-path`: serve para testar um pacote instalado
  nos caminhos padrão (no Linux, com `HOME` apontando para uma pasta de teste).

O `ajuda.mjs` abre uma cópia do patch de ajuda e confere que a porta 9478 fica livre até a
chave **TESTAR DAQUI** ser ligada, que o `[gaiaball]` dela responde e que desligar libera a
porta. Com `--com-objeto`, um patch com `[gaiaball]` abre antes, e o dele continua
respondendo com a chave ligada ou desligada.

### Pacotes e versões

`python3 pd-gaiaball/pacotes/montar.py <versão> <pasta>` monta o `.zip` da página de
versões (um só para todos os sistemas), o pacote do Deken, as somas SHA-256 e as notas da
versão. O CI faz isso a cada mudança e, numa tag `v*`, cria um **rascunho** de versão no
GitHub. O passo a passo está em [`PUBLICAR-NO-GITHUB.md`](PUBLICAR-NO-GITHUB.md) e em
[`PUBLICAR-NO-DEKEN.md`](PUBLICAR-NO-DEKEN.md).

### Notas de projeto

- A fonte da verdade do protocolo é [`firmware/docs/protocol.md`](../firmware/docs/protocol.md).
- A config-app repassa só os pacotes de movimento (`0x01`), CO₂ (`0x02`) e navegação
  (`0x07`), sem alterar, um por mensagem binária.
- O objeto não confere a origem da página (o domínio definitivo ainda não existe). A
  proteção é escutar só em `127.0.0.1` e o pedido "Apps neste dispositivo" do Chrome.
- A bola aceita duas conexões Bluetooth ao mesmo tempo: por exemplo, a config-app (que
  alimenta o Pd) e o site GaiaSenses.
- Antes desta abstração, o `[gaiaball]` era um external em C (`gaiaball.c`, `src/gb_*`,
  compilado com o pd-lib-builder). Ele foi retirado; o código continua no histórico do git.
