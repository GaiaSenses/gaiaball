O objeto **`[gaiaball]`** traz os dados da Gaiaball para dentro do Pure Data: o movimento, o sopro, os microfones, o CO₂ e o giro do globo virtual. A bola conversa por Bluetooth com a [config-app](https://gaiasenses.github.io/gaiaball/), aberta no Chrome ou no Edge, que repassa os dados ao Pd. No seu patch, cada dado chega num nome, como `[r gaia.breath]`.

O `[gaiaball]` é feito só com objetos do Pd vanilla: é um patch, e não um programa compilado. Por isso há **um pacote só**, que serve para Windows, macOS e Linux, em qualquer processador.

### Qual arquivo baixar

Os arquivos estão logo abaixo, em **Assets**. Para baixar, não é preciso ter conta no GitHub.

| Para | Baixe |
|---|---|
| Instalar à mão (Windows, macOS ou Linux) | `pd-gaiaball-@VERSAO@.zip` |
| Enviar ao Deken (só a equipe) | `gaiaball[v@VERSAO@].dek` e os dois arquivos que começam com o mesmo nome |

Descompacte o `.zip` e abra o **`LEIA-ME.html`**. Ele explica onde pôr a pasta `gaiaball` em cada sistema, como conferir que deu certo, os primeiros passos com a bola e o que fazer se algo der errado. Quem instala pelo Deken (**Ajuda → Encontrar externals** no Pd) recebe a mesma pasta e não precisa baixar nada daqui.

### O que vem no pacote

- **`gaiaball.pd`**: o objeto.
- **`LEIA-ME.html`**: instalação no Windows, no macOS e no Linux, primeiros passos e problemas comuns.
- **`TUTORIAL.html`**: o tutorial completo para músicos, sem jargão, com todos os dados (o que são, unidade, faixa e quantas vezes por segundo chegam) e receitas.
- **`gaiaball-help.pd`**: a ajuda do objeto (botão direito no `[gaiaball]` → Ajuda). Ela não ocupa a porta do seu patch: só cria um `[gaiaball]` próprio se você ligar a chave de teste.
- **`exemplos/`**: seis patches prontos e comentados. Mostram todos os dados, fazem o sopro tocar uma nota, a inclinação mexer num filtro, o CO₂ controlar o volume, o globo virar música e avisam quando a conexão falha.

### Requisitos

- Pure Data vanilla 0.51 ou mais novo (<https://puredata.info/downloads>).
- Google Chrome ou Microsoft Edge, num computador com Bluetooth, para a config-app.
- A Gaiaball com o firmware atual do projeto.

### Já tinha o `[gaiaball]` compilado?

As versões antigas eram arquivos `gaiaball.dll`, `gaiaball.pd_darwin`, `gaiaball.pd_linux` ou `gaiaball.*.so`. Se um deles ficar no computador, o Pd carrega ele no lugar do novo: apague a pasta `gaiaball` antiga inteira antes de instalar esta (o `LEIA-ME.html` mostra como conferir qual está em uso).

### Conferir o download (opcional)

A soma SHA-256 de cada arquivo (as mesmas do `SHA256SUMS.txt`):

@SOMAS@

### Licença e código-fonte

Software livre, sob a licença GPL-3.0. O `gaiaball.pd` é gerado por `tools/gerar_patch.py`; o gerador, os testes e os textos desta versão estão na pasta [`pd-gaiaball`](https://github.com/GaiaSenses/gaiaball/tree/@REF@/pd-gaiaball) do repositório e nos arquivos **Source code** abaixo.
