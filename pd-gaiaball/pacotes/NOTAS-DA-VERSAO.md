O objeto **`[gaiaball]`** traz os dados da Gaiaball para dentro do Pure Data: o movimento, o sopro, os microfones, o CO₂ e o giro do globo virtual. A bola conversa por Bluetooth com a [config-app](https://gaiasenses.github.io/gaiaball/), aberta no Chrome ou no Edge, que repassa os dados ao Pd. No seu patch, cada dado chega num nome, como `[r gaia.breath]`.

### Qual arquivo baixar

Os arquivos estão logo abaixo, em **Assets**. Para baixar, não é preciso ter conta no GitHub.

| Seu computador | Baixe |
|---|---|
| Windows 10 ou 11 (64 bits) | `pd-gaiaball-@VERSAO@-windows.zip` |
| Mac (Intel ou Apple Silicon) | `pd-gaiaball-@VERSAO@-macos.zip` |
| Linux (Intel/AMD de 64 bits, ou ARM de 64 bits como o Raspberry Pi 4 e 5) | `pd-gaiaball-@VERSAO@-linux.zip` |

Descompacte o arquivo e abra o **`LEIA-ME.html`**. Ele explica, para o seu sistema, onde pôr a pasta `gaiaball`, como conferir que deu certo, os primeiros passos com a bola e o que fazer se algo der errado. **No Mac, há um passo a mais**: liberar o objeto, que o macOS bloqueia por ter vindo da internet. O guia mostra como.

### O que vem em cada pacote

- **O objeto** compilado para aquele sistema, para o Pd comum e para o Pd de precisão dupla. O Pd escolhe sozinho o arquivo certo.
- **`LEIA-ME.html`**: instalação, primeiros passos e problemas comuns, escrito para aquele sistema.
- **`TUTORIAL.html`**: o tutorial completo para músicos, sem jargão, com todos os dados (o que são, unidade, faixa e quantas vezes por segundo chegam) e receitas.
- **`gaiaball-help.pd`**: a ajuda do objeto (botão direito no `[gaiaball]` → Ajuda).
- **`exemplos/`**: seis patches prontos e comentados. Mostram todos os dados, fazem o sopro tocar uma nota, a inclinação mexer num filtro, o CO₂ controlar o volume, o globo virar música e avisam quando a conexão falha.

### Requisitos

- Pure Data vanilla 0.51 ou mais novo (<https://puredata.info/downloads>).
- Google Chrome ou Microsoft Edge, num computador com Bluetooth, para a config-app.
- A Gaiaball com o firmware atual do projeto.

### Ainda não está no Deken

O `[gaiaball]` ainda não aparece em **Ajuda → Encontrar externals** (o Deken, a "loja" de objetos do Pd). Por enquanto, a instalação é pelos pacotes acima. Quando ele chegar ao Deken, quem instalou por aqui deve apagar a cópia manual antes de instalar por lá, para não ficar com duas versões.

### Conferir o download (opcional)

A soma SHA-256 de cada arquivo:

@SOMAS@

### Licença e código-fonte

Software livre, sob a licença GPL-3.0. O código-fonte desta versão está nos arquivos **Source code** abaixo e na pasta [`pd-gaiaball`](https://github.com/GaiaSenses/gaiaball/tree/@REF@/pd-gaiaball) do repositório.
