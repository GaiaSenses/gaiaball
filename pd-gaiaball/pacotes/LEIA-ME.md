# O objeto `[gaiaball]` para Pure Data

**Versão @VERSAO@ · pacote para [Windows (64 bits)]{.so-windows}[Mac (Intel e Apple Silicon)]{.so-macos}[Linux (Intel/AMD e ARM de 64 bits)]{.so-linux}**

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

Este guia mostra como instalar o objeto
[no Windows]{.so-windows}[no Mac]{.so-macos}[no Linux]{.so-linux}, conferir se deu certo e
dar os primeiros passos. O passo a passo completo, com todos os dados, receitas e o que fazer
quando algo dá errado, está no **[tutorial](gaiaball/TUTORIAL.html)**, que vem neste pacote.

## O que tem neste pacote

| Arquivo | O que é |
|---|---|
| `LEIA-ME.html` | este guia |
| `gaiaball/` | **a pasta que você vai instalar** (tudo o que vem abaixo fica dentro dela) |
| [`gaiaball.dll` e `gaiaball.windows-amd64-64.dll`]{.so-windows}[`gaiaball.pd_darwin` e `gaiaball.darwin-fat-64.so`]{.so-macos}[`gaiaball.pd_linux`, `gaiaball.l_arm64` e os três `gaiaball.linux-*.so`]{.so-linux} | o objeto em si, para o Pd comum e para o Pd de precisão dupla[, em computadores Intel/AMD e ARM]{.so-linux}. O Pd escolhe sozinho o arquivo certo: **não apague nenhum** |
| `gaiaball-help.pd` | a ajuda do objeto (botão direito no `[gaiaball]` → **Ajuda**) |
| `exemplos/` | seis patches prontos e comentados, do mais simples ao mais completo |
| `TUTORIAL.html` | o tutorial completo, escrito para músicos, sem jargão |
| `LICENSE.txt` | a licença (GPL-3.0) |

## Do que você precisa

- **A Gaiaball.**
- **Um computador com Bluetooth**: [Windows 10 ou 11, de 64 bits]{.so-windows}[um Mac com
  processador Intel ou Apple (M1, M2, M3…)]{.so-macos}[Linux num computador Intel/AMD de 64
  bits ou ARM de 64 bits (como o Raspberry Pi 4 ou 5 com o sistema de 64 bits)]{.so-linux}.
- **Pure Data "vanilla", versão 0.51 ou mais nova.** *Vanilla* é o Pd oficial, baixado em
  <https://puredata.info/downloads>.
  [Use o instalador de 64 bits, que é o padrão hoje.]{.so-windows}
  [Também serve o Pd dos pacotes da sua distribuição (`sudo apt install puredata`).]{.so-linux}
- **Google Chrome ou Microsoft Edge.** O Firefox e o Safari não deixam sites usarem o
  Bluetooth.

## Instalar (uma vez)

::: so-windows
1. **Descompacte o pacote.** Clique com o botão direito no arquivo
   `pd-gaiaball-@VERSAO@-windows.zip` → **Extrair tudo…** → **Extrair**. Não use os
   arquivos direto de dentro do `.zip`: o Windows deixa abrir, mas o Pd não os encontra.
2. **Abra a pasta de objetos do Pd.** No Explorador de Arquivos, clique na barra de
   endereço (onde aparece o caminho da pasta), digite `%AppData%` e aperte **Enter**. Abre
   a pasta `C:\Users\<seu nome>\AppData\Roaming`.
3. Se ali não houver uma pasta chamada **`Pd`**, crie: botão direito num espaço vazio →
   **Novo → Pasta** → digite `Pd`.
4. **Copie a pasta `gaiaball`** (a que veio neste pacote, ao lado deste guia) para dentro
   da pasta `Pd`. O objeto tem que ficar exatamente aqui:

   ```
   C:\Users\<seu nome>\AppData\Roaming\Pd\gaiaball\gaiaball.dll
   ```

   Copie só a pasta `gaiaball`, e não a pasta do pacote inteiro: com uma pasta a mais no
   meio do caminho, o Pd não acha o objeto.
5. Se o Pd estava aberto, feche e abra de novo.

O Pd procura objetos nessa pasta sozinho: não é preciso mexer em **Preferências →
Caminho**.
:::

::: so-macos
1. **Descompacte o pacote.** Clique duas vezes em `pd-gaiaball-@VERSAO@-macos.zip` (o
   Safari às vezes já descompacta sozinho). Aparece uma pasta com este guia e a pasta
   `gaiaball`.
2. **Abra a pasta de objetos do Pd.** No Finder, menu **Ir → Ir para a Pasta…**
   (Shift+Cmd+G), digite `~/Library` e aperte **Enter**. Em português, o Finder chama essa
   pasta de **Biblioteca**.
3. Se ali não houver uma pasta chamada **`Pd`**, crie: menu **Arquivo → Nova Pasta** →
   digite `Pd`.
4. **Copie a pasta `gaiaball`** (a que veio neste pacote, ao lado deste guia) para dentro
   da pasta `Pd`. O objeto tem que ficar exatamente aqui:

   ```
   ~/Library/Pd/gaiaball/gaiaball.pd_darwin
   ```

5. **Libere o objeto. Este passo é obrigatório no Mac.** O macOS marca tudo o que foi
   baixado da internet e não deixa o Pd abrir um objeto marcado que não foi assinado por
   um desenvolvedor registrado na Apple. Para tirar a marca, abra o **Terminal**
   (**Aplicativos → Utilitários → Terminal**), cole a linha abaixo e aperte **Enter**:

   ```
   xattr -dr com.apple.quarantine ~/Library/Pd/gaiaball
   ```

   O Terminal não mostra nada depois: é normal, deu certo. O comando só tira a marca de
   "baixado da internet" dos arquivos dessa pasta; ele não muda nada no seu Mac. Se um dia
   você instalar uma versão nova do `[gaiaball]`, repita este passo.
6. Se o Pd estava aberto, feche (**Cmd+Q**) e abra de novo.

O Pd procura objetos nessa pasta sozinho: não é preciso mexer em **Preferências →
Caminho**.
:::

::: so-linux
1. **Descompacte o pacote**, se ainda não descompactou. Pelo gerenciador de arquivos
   (botão direito → **Extrair aqui**) ou no terminal:

   ```
   unzip pd-gaiaball-@VERSAO@-linux.zip -d pd-gaiaball-@VERSAO@-linux
   ```

2. **Copie a pasta `gaiaball` para a pasta de objetos do Pd.** No terminal, dentro da pasta
   descompactada (a que tem este guia):

   ```
   mkdir -p ~/.local/lib/pd/extra
   cp -r gaiaball ~/.local/lib/pd/extra/
   ```

   O objeto tem que ficar exatamente aqui:

   ```
   ~/.local/lib/pd/extra/gaiaball/gaiaball.pd_linux
   ```

   (A pasta `.local` começa com ponto, por isso o gerenciador de arquivos a esconde. Para
   vê-la, aperte **Ctrl+H**.)
3. Se o Pd estava aberto, feche e abra de novo.

O Pd procura objetos nessa pasta sozinho: não é preciso mexer em **Preferências →
Caminho**.
:::

## Conferir a instalação

1. Abra o Pd e crie um patch novo: **Arquivo → Novo**.
2. Aperte **[Ctrl]{.so-windows .so-linux}[Cmd]{.so-macos}+1** para criar um objeto, digite
   `gaiaball` e clique fora da caixa.
3. **Borda contínua:** deu certo. **Borda tracejada**, com a mensagem
   `gaiaball ... couldn't create` no console (a janela principal do Pd, onde aparecem as
   mensagens): o Pd não achou o objeto. Veja [Se algo der errado](#se-algo-der-errado).
4. Clique com o botão direito no `[gaiaball]` → **Ajuda**. Abre o patch de ajuda, com o
   passo a passo da conexão e todos os dados que chegam.

## Primeiros passos

1. **Ligue a bola parada**, apoiada numa superfície plana. A posição em que ela liga vira
   o "zero" da orientação e do globo virtual.
2. **Abra o primeiro exemplo no Pd**: o `gaiaball/exemplos/01-ver-tudo.pd`. Pode clicar
   duas vezes no arquivo ou usar **Arquivo → Abrir** no Pd. Deixe esse patch aberto.
3. **Abra a config-app no Chrome ou no Edge**: <https://gaiasenses.github.io/gaiaball/>.
   No painel **Conexão**, clique em **Conectar** e escolha a sua bola (o nome de fábrica é
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
config-app inventa dados parecidos com os da bola e os manda ao Pd pelo mesmo caminho,
com um "sopro" a cada 6 segundos. Ótimo para testar os exemplos.

## Os exemplos

Ficam em `gaiaball/exemplos/`. Cada um é um patch comentado: os textos dentro dele explicam
cada parte. Os que fazem som pedem para ligar o som do Pd: clique na mensagem `dsp 1` (ou
**[Ctrl]{.so-windows .so-linux}[Cmd]{.so-macos}+/**). Comece com o volume do computador
baixo.

| Exemplo | O que faz |
|---|---|
| `01-ver-tudo.pd` | mostra todos os dados e o estado da conexão. Não faz som. Comece por ele |
| `02-sopro-toca-nota.pd` | cada sopro na bola toca uma nota |
| `03-inclinacao-filtro.pd` | inclinar a bola mexe na altura e na ressonância de um filtro |
| `04-co2-volume.pd` | o CO₂ controla o volume de um som contínuo |
| `05-globo-altura.pd` | o globo vira música: a latitude é a nota, a longitude é o lado do som |
| `06-estado-da-conexao.pd` | mostra se está conectado e avisa quando os dados falham |

O [tutorial, seção 10](gaiaball/TUTORIAL.html#10-exemplos-prontos) diz o que experimentar
em cada um. A seção 9 tem receitas para montar os seus.

## Se algo der errado

**O Pd diz `gaiaball ... couldn't create` (a caixa fica tracejada).** O Pd não achou o
objeto. Confira se ele está exatamente em
[`C:\Users\<seu nome>\AppData\Roaming\Pd\gaiaball\gaiaball.dll`]{.so-windows}[`~/Library/Pd/gaiaball/gaiaball.pd_darwin`]{.so-macos}[`~/.local/lib/pd/extra/gaiaball/gaiaball.pd_linux`]{.so-linux},
sem uma pasta a mais no meio. Depois, em
**[Arquivo → Preferências]{.so-windows .so-linux}[Pd → Preferências]{.so-macos} →
Caminho** (em inglês, *Preferences → Path*), confira que a opção **Usar caminhos padrão**
(*Use standard paths*) está ligada, como vem de fábrica. Feche e abra o Pd de novo.

::: so-macos
**O Mac avisa que "gaiaball.pd_darwin" não pode ser aberto, ou que a Apple não pode
verificá-lo.** Faltou o passo 5 da instalação. Rode o comando `xattr` daquele passo, feche o
Pd (**Cmd+Q**) e abra de novo. Se preferir não usar o Terminal: logo depois do aviso, abra
**Ajustes do Sistema → Privacidade e Segurança**. Se aparecer ali uma linha sobre o
`gaiaball.pd_darwin` com o botão **Abrir Mesmo Assim** (ou **Permitir Mesmo Assim**),
clique nele e abra o Pd de novo.
:::

::: so-linux
**O console fala em `wrong ELF class` ou `cannot open shared object file`.** O seu
computador não é Intel/AMD de 64 bits nem ARM de 64 bits. Por exemplo: um Raspberry Pi com o
sistema de **32 bits**. Instale o Raspberry Pi OS de 64 bits.

**Na config-app, o botão Conectar não mostra nenhum aparelho.** No Linux, o Chrome às vezes
traz o Bluetooth para sites desligado. Abra `chrome://flags`, procure **Experimental Web
Platform features**, mude para **Enabled** e reinicie o Chrome. Confira também que o
Bluetooth do computador está ligado.
:::

**A config-app mostra "Pd: aguardando".** Ela procura o Pd e não acha: o Pd está fechado,
não há nenhum patch aberto com `[gaiaball]`, ou a porta é diferente nos dois lados
(`[gaiaball]` usa a 9478, a mesma da config-app).

**A config-app mostra "Pd: bloqueado pelo Chrome".** A permissão **Apps neste dispositivo**
foi negada. Clique no ícone à esquerda do endereço → **Configurações do site** → **Apps
neste dispositivo** → **Permitir**, e recarregue a página.

**Só chega `gaia.co2`.** O sensor de movimento da bola está desligado, como vem de fábrica.
Veja o item 4 dos [Primeiros passos](#primeiros-passos).

Os outros casos estão no [tutorial, seção 11](gaiaball/TUTORIAL.html#11-problemas-comuns).

## Quando o `[gaiaball]` chegar ao Deken

O Deken é a "loja" de objetos que fica dentro do Pd (**Ajuda → Encontrar externals**). O
`[gaiaball]` ainda não está lá; quando estiver, ele vai poder ser instalado por ela. Se você
instalar pelo Deken, **apague antes a cópia instalada por este pacote** (a pasta
[`%AppData%\Pd\gaiaball`]{.so-windows}[`~/Library/Pd/gaiaball`]{.so-macos}[`~/.local/lib/pd/extra/gaiaball`]{.so-linux}).
Com duas cópias, o Pd usa só uma delas, e fica difícil saber qual.

## Licença e código-fonte

O `[gaiaball]` faz parte do projeto GaiaSenses e é software livre, sob a licença GPL-3.0
(arquivo `gaiaball/LICENSE.txt`). O código-fonte desta versão está em
<https://github.com/GaiaSenses/gaiaball/tree/@REF@/pd-gaiaball>.
