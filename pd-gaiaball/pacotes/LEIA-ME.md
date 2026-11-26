# O objeto `[gaiaball]` para Pure Data

**Versão @VERSAO@ · um pacote só, para Windows, macOS e Linux**

O `[gaiaball]` traz os dados da Gaiaball para dentro do Pure Data (o "Pd"): o movimento, o
sopro, os microfones, o CO₂ e o giro do globo virtual. Quem conversa com a bola por
Bluetooth é a **config-app**, uma página que roda no Chrome ou no Edge. Ela repassa os dados
ao `[gaiaball]`, e no seu patch cada dado chega num nome, como `[r gaia.breath]` (o sopro)
ou `[r gaia.co2]` (o CO₂).

```
Gaiaball
   │  Bluetooth
   ▼
config-app, aberta no Chrome ou no Edge
   │  pelo próprio computador (não sai dele)
   ▼
[gaiaball], no Pd  ──>  [r gaia.breath]  [r gaia.co2]  [r gaia.lat] ...
```

O `[gaiaball]` é feito só com objetos que já vêm com o Pd: ele é um patch (o arquivo
`gaiaball.pd`), e não um programa compilado. Por isso o mesmo pacote serve para Windows,
macOS e Linux, em qualquer processador, e não há nada para liberar no sistema.

Este guia mostra como instalar, conferir se deu certo e dar os primeiros passos. O passo a
passo completo, com todos os dados, receitas e o que fazer quando algo dá errado, está no
**[tutorial](gaiaball/TUTORIAL.html)**, que vem neste pacote.

## Sumário

1. [O que tem neste pacote](#o-que-tem-neste-pacote)
2. [Do que você precisa](#do-que-você-precisa)
3. [Já tinha o `[gaiaball]` antigo?](#já-tinha-o-gaiaball-antigo)
4. [Instalar (uma vez)](#instalar-uma-vez): [Windows](#windows), [macOS](#macos), [Linux](#linux)
5. [Conferir a instalação](#conferir-a-instalação)
6. [Primeiros passos](#primeiros-passos)
7. [Os exemplos](#os-exemplos)
8. [Se algo der errado](#se-algo-der-errado)

## O que tem neste pacote

| Arquivo | O que é |
|---|---|
| `LEIA-ME.html` | este guia |
| `gaiaball/` | **a pasta que você vai instalar** (tudo o que vem abaixo fica dentro dela) |
| `gaiaball.pd` | o objeto em si |
| `gaiaball-help.pd` | a ajuda do objeto (botão direito no `[gaiaball]` → **Ajuda**) |
| `exemplos/` | seis patches prontos e comentados, do mais simples ao mais completo |
| `TUTORIAL.html` | o tutorial completo, escrito para músicos, sem jargão |
| `LICENSE.txt` | a licença (GPL-3.0) |

## Do que você precisa

- **A Gaiaball.**
- **Um computador com Bluetooth**, com Windows, macOS ou Linux.
- **Pure Data "vanilla", versão 0.51 ou mais nova.** *Vanilla* é o Pd oficial, baixado em
  <https://puredata.info/downloads>. No Linux, também serve o Pd dos pacotes da sua
  distribuição (`sudo apt install puredata`).
- **Google Chrome ou Microsoft Edge.** O Firefox e o Safari não deixam sites usarem o
  Bluetooth.

## Já tinha o `[gaiaball]` antigo?

As primeiras versões do `[gaiaball]` eram um programa compilado: arquivos chamados
`gaiaball.dll`, `gaiaball.pd_darwin`, `gaiaball.pd_linux` ou `gaiaball.` seguido de algo
terminado em `.so`. **Se um desses arquivos ainda estiver no computador, o Pd carrega ele
no lugar do novo.** Antes de instalar:

- apague a pasta `gaiaball` antiga inteira (não copie a nova por cima);
- apague também esses arquivos se você os pôs na pasta dos seus patches.

Para saber qual está em uso: o antigo escreve
`gaiaball: aguardando a config-app em ws://127.0.0.1:9478` no console (a janela principal
do Pd) quando é criado. O novo não escreve nada, e um clique nele abre a janela dele.

## Instalar (uma vez)

**Pelo Deken.** Se o `[gaiaball]` já estiver no Deken (a "loja" de objetos que fica dentro
do Pd), é o caminho mais curto: **Ajuda → Encontrar externals** (no Pd em inglês: *Help →
Find externals*), procure `gaiaball` e clique em **Instalar**. O Deken põe a pasta numa
pasta que o Pd já procura. Se ele perguntar se deve acrescentar a pasta `gaiaball` ao
caminho de busca, tanto faz: o objeto funciona das duas formas. Depois, pule para
[Conferir a instalação](#conferir-a-instalação).

**Por este pacote.** Copie a pasta `gaiaball` para a pasta de objetos que o Pd procura
sozinho, como abaixo. Não é preciso mexer em **Preferências → Caminho**.

### Windows

1. **Descompacte o pacote.** Clique com o botão direito no arquivo
   `pd-gaiaball-@VERSAO@.zip` → **Extrair tudo…** → **Extrair**. Não use os arquivos direto
   de dentro do `.zip`: o Windows deixa abrir, mas o Pd não os encontra.
2. **Abra a pasta de objetos do Pd.** No Explorador de Arquivos, clique na barra de endereço
   (onde aparece o caminho da pasta), digite `%AppData%` e aperte **Enter**. Abre a pasta
   `C:\Users\<seu nome>\AppData\Roaming`.
3. Se ali não houver uma pasta chamada **`Pd`**, crie: botão direito num espaço vazio →
   **Novo → Pasta** → digite `Pd`.
4. **Copie a pasta `gaiaball`** (a que veio neste pacote, ao lado deste guia) para dentro da
   pasta `Pd`. O objeto tem que ficar exatamente aqui:

   ```
   C:\Users\<seu nome>\AppData\Roaming\Pd\gaiaball\gaiaball.pd
   ```

   Copie só a pasta `gaiaball`, e não a pasta do pacote inteiro: com uma pasta a mais no
   meio do caminho, o Pd não acha o objeto.
5. Se o Pd estava aberto, feche e abra de novo.

### macOS

1. **Descompacte o pacote.** Clique duas vezes em `pd-gaiaball-@VERSAO@.zip` (o Safari às
   vezes já descompacta sozinho). Aparece uma pasta com este guia e a pasta `gaiaball`.
2. **Abra a pasta de objetos do Pd.** No Finder, menu **Ir → Ir para a Pasta…**
   (Shift+Cmd+G), digite `~/Library` e aperte **Enter**. Em português, o Finder chama essa
   pasta de **Biblioteca**.
3. Se ali não houver uma pasta chamada **`Pd`**, crie: menu **Arquivo → Nova Pasta** →
   digite `Pd`.
4. **Copie a pasta `gaiaball`** (a que veio neste pacote, ao lado deste guia) para dentro da
   pasta `Pd`. O objeto tem que ficar exatamente aqui:

   ```
   ~/Library/Pd/gaiaball/gaiaball.pd
   ```

5. Se o Pd estava aberto, feche (**Cmd+Q**) e abra de novo.

O macOS não bloqueia o `[gaiaball]`: ele é um arquivo de texto que o Pd lê, e não um
programa. Não há nada para liberar em **Privacidade e Segurança**.

### Linux

1. **Descompacte o pacote**, se ainda não descompactou. Pelo gerenciador de arquivos (botão
   direito → **Extrair aqui**) ou no terminal:

   ```
   unzip pd-gaiaball-@VERSAO@.zip -d pd-gaiaball-@VERSAO@
   ```

2. **Copie a pasta `gaiaball` para a pasta de objetos do Pd.** No terminal, dentro da pasta
   descompactada (a que tem este guia):

   ```
   mkdir -p ~/.local/lib/pd/extra
   cp -r gaiaball ~/.local/lib/pd/extra/
   ```

   O objeto tem que ficar exatamente aqui:

   ```
   ~/.local/lib/pd/extra/gaiaball/gaiaball.pd
   ```

   (A pasta `.local` começa com ponto, por isso o gerenciador de arquivos a esconde. Para
   vê-la, aperte **Ctrl+H**.) A pasta `~/pd-externals` também serve.
3. Se o Pd estava aberto, feche e abra de novo.

**Outra pasta?** Também funciona, desde que a pasta que **contém** `gaiaball` esteja em
**Preferências → Caminho** (em inglês, *Preferences → Path*). É o caso, por exemplo, da
pasta `Documentos/Pd/externals`, quando o Pd a mostra nessa lista.

## Conferir a instalação

1. Abra o Pd e crie um patch novo: **Arquivo → Novo**.
2. Aperte **Ctrl+1** (no Mac, **Cmd+1**) para criar um objeto, digite `gaiaball` e clique
   fora da caixa.
3. **Borda contínua:** deu certo. **Borda tracejada**, com a mensagem
   `gaiaball ... couldn't create` no console (a janela principal do Pd, onde aparecem as
   mensagens): o Pd não achou o objeto. Veja [Se algo der errado](#se-algo-der-errado).
4. Clique com o botão direito no `[gaiaball]` → **Ajuda**. Abre o patch de ajuda, com o
   passo a passo da conexão e todos os dados que chegam. A ajuda não ocupa a porta do seu
   patch: ela só cria um `[gaiaball]` próprio se você ligar a chave **TESTAR DAQUI**.

## Primeiros passos

1. **Ligue a bola parada**, apoiada numa superfície plana. A posição em que ela liga vira o
   "zero" da orientação e do globo virtual.
2. **Abra o primeiro exemplo no Pd**: o `gaiaball/exemplos/01-ver-tudo.pd`. Pode clicar duas
   vezes no arquivo ou usar **Arquivo → Abrir** no Pd. Deixe esse patch aberto.
3. **Abra a config-app no Chrome ou no Edge**: <https://gaiasenses.github.io/gaiaball/>. No
   painel **Conexão**, clique em **Conectar** e escolha a sua bola (o nome de fábrica é
   `Globo_1`).
4. **Só na primeira vez com uma bola nova: ligue o sensor de movimento.** A bola sai de
   fábrica com ele desligado, e sem ele só chega o CO₂. O passo a passo (com o PIN de
   fábrica, `1234`) está no
   [tutorial, passo 5](gaiaball/TUTORIAL.html#passo-5-só-na-primeira-vez-ligue-o-sensor-de-movimento).
5. **Na primeira vez, o Chrome pergunta** se o site pode acessar **Apps neste
   dispositivo**: clique em **Permitir**. É o que deixa a página conversar com o Pd. A
   chave **Enviar ao Pd** da config-app já vem ligada.
6. **Pronto.** No alto da config-app aparece **"Pd: conectado · N pacotes/s"**, e no Pd o
   quadrado "conectado" do exemplo acende e os números começam a mudar. Incline a bola,
   gire, sopre.

**Sem a bola por perto?** Abra <https://gaiasenses.github.io/gaiaball/?demo=1>: a
config-app inventa dados parecidos com os da bola e os manda ao Pd pelo mesmo caminho, com
um "sopro" a cada 6 segundos. Ótimo para testar os exemplos.

## Os exemplos

Ficam em `gaiaball/exemplos/`. Cada um é um patch comentado: os textos dentro dele explicam
cada parte. Os que fazem som pedem para ligar o som do Pd: clique na mensagem `dsp 1` (ou
**Ctrl+/**; no Mac, **Cmd+/**). Comece com o volume do computador baixo.

| Exemplo | O que faz |
|---|---|
| `01-ver-tudo.pd` | mostra todos os dados e o estado da conexão. Não faz som. Comece por ele |
| `02-sopro-toca-nota.pd` | cada sopro na bola toca uma nota |
| `03-inclinacao-filtro.pd` | inclinar a bola mexe na altura e na ressonância de um filtro |
| `04-co2-volume.pd` | o CO₂ controla o volume de um som contínuo |
| `05-globo-altura.pd` | o globo vira música: a latitude é a nota, a longitude é o lado do som |
| `06-estado-da-conexao.pd` | mostra se está conectado e avisa quando os dados falham |

Cada exemplo tem o seu `[gaiaball]`. Com vários abertos, só o primeiro escuta: os outros
mostram `Address already in use` no console, e os dados chegam a todos do mesmo jeito. Se
você fechar o primeiro, clique no `[gaiaball]` de um dos que ficaram (fora do modo de
edição) e, na janela que abre, em **reconectar**.

O [tutorial, seção 10](gaiaball/TUTORIAL.html#10-exemplos-prontos) diz o que experimentar
em cada um. A seção 9 tem receitas para montar os seus.

## Se algo der errado

**O Pd diz `gaiaball ... couldn't create` (a caixa fica tracejada).** O Pd não achou o
objeto. Confira se ele está exatamente em `C:\Users\<seu nome>\AppData\Roaming\Pd\gaiaball\gaiaball.pd`
(Windows), `~/Library/Pd/gaiaball/gaiaball.pd` (macOS) ou
`~/.local/lib/pd/extra/gaiaball/gaiaball.pd` (Linux), sem uma pasta a mais no meio. Depois,
em **Preferências → Caminho** (no Mac, **Pd → Preferências → Caminho**; em inglês,
*Preferences → Path*), confira que a opção **Usar caminhos padrão** (*Use standard paths*)
está ligada, como vem de fábrica. Feche e abra o Pd de novo.

**O console mostra `gaiaball: aguardando a config-app em ws://...`.** É o `[gaiaball]`
antigo, compilado, que ficou no computador. Veja
[Já tinha o `[gaiaball]` antigo?](#já-tinha-o-gaiaball-antigo).

**O console mostra `netreceive: listen failed: Address already in use`.** Outro
`[gaiaball]` (em outro patch aberto) já usa a porta 9478. Enquanto ele estiver aberto, os
dados chegam do mesmo jeito. Se você fechar aquele patch, clique no `[gaiaball]` que ficou
(fora do modo de edição) e em **reconectar**. Se a porta for de outro programa, use outra
porta nos dois lados: `[gaiaball 9500]` no Pd e 9500 na config-app (painel **Conexão → Pd →
Porta**).

**A config-app mostra "Pd: aguardando".** Ela procura o Pd e não acha: o Pd está fechado,
não há nenhum patch aberto com `[gaiaball]`, o `[gaiaball]` que ficou aberto não está
escutando (veja o item anterior), ou a porta é diferente nos dois lados (`[gaiaball]` usa a
9478, a mesma da config-app).

**A config-app mostra "Pd: bloqueado pelo Chrome".** A permissão **Apps neste dispositivo**
foi negada. Clique no ícone à esquerda do endereço → **Configurações do site** → **Apps
neste dispositivo** → **Permitir**, e recarregue a página.

**Só chega `gaia.co2`.** O sensor de movimento da bola está desligado, como vem de fábrica.
Veja o item 4 dos [Primeiros passos](#primeiros-passos).

**No Linux, o botão Conectar da config-app não mostra nenhum aparelho.** O Chrome às vezes
traz o Bluetooth para sites desligado no Linux. Abra `chrome://flags`, procure
**Experimental Web Platform features**, mude para **Enabled** e reinicie o Chrome. Confira
também que o Bluetooth do computador está ligado.

Os outros casos estão no [tutorial, seção 11](gaiaball/TUTORIAL.html#11-problemas-comuns).

## Instalou por aqui e depois pelo Deken?

Fique com uma cópia só. Antes de instalar pelo Deken, apague a pasta `gaiaball` que você
copiou com este pacote. Com duas cópias, o Pd usa só uma delas, e fica difícil saber qual.

## Licença e código-fonte

O `[gaiaball]` faz parte do projeto GaiaSenses e é software livre, sob a licença GPL-3.0
(arquivo `gaiaball/LICENSE.txt`). O próprio `gaiaball.pd` é o código: ele é gerado pelo
script `tools/gerar_patch.py` do repositório, onde também ficam os testes:
<https://github.com/GaiaSenses/gaiaball/tree/@REF@/pd-gaiaball>.
