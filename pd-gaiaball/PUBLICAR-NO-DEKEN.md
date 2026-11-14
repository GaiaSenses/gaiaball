# Como publicar o `[gaiaball]` no Deken

Este guia é para quem mantém o projeto. Ele explica, passo a passo, como colocar uma versão
nova do objeto `[gaiaball]` no Deken, o "gerenciador de pacotes" do Pure Data. Depois de
publicado, qualquer pessoa instala o objeto de dentro do Pd, sem terminal:
**Ajuda → Encontrar externals → "gaiaball" → Instalar**.

> Quem só **usa** o `[gaiaball]` não precisa ler isto: veja o [README](README.md).

## 1. Como o Deken funciona, em uma frase

O Deken não é uma loja central: cada autor guarda os seus pacotes na **própria conta do
site [puredata.info](https://puredata.info/)**. A busca do Pd ("Encontrar externals") procura
nos pacotes de todas as contas. Publicar é, portanto, **enviar arquivos para a sua conta no
puredata.info**, com um nome que diz o que tem dentro.

## 2. O que você precisa

| Item | Para quê | Onde |
|---|---|---|
| Conta no puredata.info | É nela que o pacote fica guardado | Seção 3 |
| A ferramenta `deken` | Monta o pacote e faz o envio | Seção 4 |
| A pasta `gaiaball/` com os binários dos três sistemas | É o conteúdo do pacote | Seção 5 |

## 3. Criar a conta no puredata.info (uma vez)

1. Abra <https://puredata.info/join_form> e preencha o formulário.
2. **O cadastro não é automático.** O pedido vai para os administradores do portal. Quando
   for aprovado, uma senha é gerada e chega por e-mail. Peça a conta **com antecedência**,
   antes do dia em que quiser publicar.
3. Ao receber a senha, entre no site e troque-a por uma sua.

**Dica:** todo conteúdo fica associado à conta que o enviou. Se o pacote é do projeto, e não
de uma pessoa, vale criar uma conta do projeto (por exemplo, "gaiasenses"). Assim o pacote
não depende de quem estiver no projeto no futuro.

## 4. Instalar a ferramenta `deken` (uma vez)

Escolha **uma** das opções:

- **Linux / WSL (Debian, Ubuntu):**
  ```sh
  sudo apt-get install deken
  ```
- **Linux, macOS ou Windows com Python 3, via `pipx`** (versão mais nova):
  ```sh
  python3 -m pip install --user pipx
  pipx install deken
  ```
- **Windows ou macOS sem Python:** baixe o executável pronto na página de
  [releases do deken](https://github.com/pure-data/deken/releases).
- **Docker:**
  ```sh
  docker pull registry.git.iem.at/pd/deken
  ```

Confira:

```sh
deken --help
```

## 5. Preparar a pasta do pacote

O pacote é **uma pasta chamada exatamente `gaiaball`**, com tudo dentro:

```
gaiaball/
├── gaiaball.<extensão de cada sistema>   ← um binário por sistema/processador/precisão
├── gaiaball-help.pd                      ← patch de ajuda (o Pd abre com clique-direito → Ajuda)
├── README.md
├── LICENSE
└── *.c, *.h, Makefile                    ← o código-fonte (ver "Fontes", abaixo)
```

- **De onde vêm os binários:** o CI do GitHub compila o `[gaiaball]` para Windows, Linux
  (Intel/AMD e ARM) e macOS (Intel + Apple Silicon), nas duas precisões do Pd. Baixe o
  artefato da execução do CI na aba **Actions** do repositório, na versão que você quer
  publicar, e descompacte: ele já vem com a pasta `gaiaball/` montada.
- **O `deken` descobre sozinho para quais sistemas o pacote serve**, olhando os binários
  dentro da pasta. Você não escreve isso à mão.
- **Fontes:** o `deken` exige que cada publicação inclua também o código-fonte (a
  pseudoarquitetura `Sources`). Como a pasta já contém os `.c`, ele detecta isso
  automaticamente.
- **Descrição na busca:** o `deken` gera a lista de objetos a partir do `gaiaball-help.pd`. A
  descrição curta vem do comentário `DESCRIPTION` dentro do subpatch `[pd META]` do patch de
  ajuda. Confira que ele existe e está em uma linha.

## 6. Escolher o número da versão

- Use três números: `0.1.0`, `0.1.1`, `0.2.0`, …
  - Corrigiu um defeito → aumente o último (`0.1.0` → `0.1.1`).
  - Acrescentou algo → aumente o do meio (`0.1.1` → `0.2.0`).
  - Mudou algo de um jeito que quebra patches existentes → aumente o primeiro.
- **Nunca publique duas vezes o mesmo número.** Se errou, publique o número seguinte.
- Use o mesmo número na tag do Git (`git tag v0.1.0`), para saber de qual código veio o
  pacote.

## 7. Gerar o pacote

Na pasta que **contém** `gaiaball/`:

```sh
deken package --version 0.1.0 gaiaball/
```

Isso cria quatro arquivos (o último só se você tiver uma chave GPG configurada):

| Arquivo | O que é |
|---|---|
| `gaiaball[v0.1.0](…).dek` | o pacote (um arquivo compactado da pasta) |
| `gaiaball[v0.1.0](…).dek.txt` | a lista de objetos, usada pela busca do Pd |
| `gaiaball[v0.1.0](…).dek.sha256` | a soma de verificação do pacote |
| `gaiaball[v0.1.0](…).dek.asc` | a assinatura GPG (opcional) |

**Confira o nome do `.dek` antes de enviar.** Entre parênteses devem aparecer todos os
sistemas e o `Sources`, algo como:

```
gaiaball[v0.1.0](Darwin-amd64-32)(Darwin-arm64-32)(Linux-amd64-32)(Linux-arm64-32)(Windows-amd64-32)(Darwin-amd64-64)(Darwin-arm64-64)(Linux-amd64-64)(Linux-arm64-64)(Windows-amd64-64)(Sources).dek
```

O número final (`32` ou `64`) é a precisão dos números do Pd: `32` é o Pd comum, `64` é o
Pd de precisão dupla. Se faltar um sistema, o binário dele não está na pasta (ou está com a
extensão errada): **não envie**, volte ao passo 5.

## 8. Enviar

```sh
deken upload "gaiaball[v0.1.0](…).dek"
```

(use o nome completo do arquivo, entre aspas por causa dos colchetes e parênteses)

- O `deken` pergunta o **usuário** e a **senha** do puredata.info.
- Ele envia o `.dek` **e** os arquivos auxiliares (`.txt`, `.sha256`, `.asc`) de uma vez.
- O pacote vai para `https://puredata.info/Members/<seu-usuário>/software/gaiaball/`.
- Se você não usa GPG e o `deken` reclamar da assinatura, acrescente `--no-sign-gpg`.

**Sobre guardar a senha:** o `deken` aceita um arquivo `~/.config/deken/config` com
`username = …` e `password = …`. Ele fica em texto puro no seu computador; prefira digitar a
senha a cada envio.

## 9. Conferir que funcionou

1. Num computador **que ainda não tem** o `[gaiaball]`, abra o Pd.
2. **Ajuda → Encontrar externals** (no Pd em inglês: **Help → Find externals**).
3. Procure `gaiaball`. A versão nova deve aparecer. Se não aparecer de imediato, tente de
   novo mais tarde: o índice da busca é atualizado pelo portal.
4. Instale, crie `[gaiaball]` num patch e abra a ajuda (clique-direito → Ajuda).
5. Conecte a bola pela config-app e confira que os `[r gaia.*]` recebem dados.

Faça isso nos três sistemas sempre que possível. O macOS é o que menos testamos.

## 10. Publicar uma versão nova

Repita os passos 5 a 9 com um número de versão maior. As versões antigas continuam
disponíveis; a busca do Pd oferece a mais nova por padrão.

## 11. Corrigir um envio errado

Entre em <https://puredata.info/>, abra
`Members/<seu-usuário>/software/gaiaball/<versão>` e apague os arquivos daquela versão pelo
próprio site. Depois publique o número seguinte. Não reaproveite o número apagado: quem já
baixou aquela versão teria dois pacotes diferentes com o mesmo nome.

## 12. Opcional, no futuro: publicar pelo CI

O `deken` lê o usuário e a senha das variáveis de ambiente `DEKEN_USERNAME` e
`DEKEN_PASSWORD`. Dá para o GitHub Actions publicar sozinho a cada tag `v*`, guardando as
duas como *secrets* do repositório e rodando a imagem Docker do `deken`
(`registry.git.iem.at/pd/deken`). Só vale a pena quando as versões ficarem frequentes. Até
lá, o envio manual acima é mais seguro: você confere o nome do pacote antes de publicar.

## Referências

- [Documentação do deken](https://deken.readthedocs.io/)
- [deken: guia de linha de comando](https://github.com/pure-data/deken/blob/main/docs/cmdline.md)
- [deken: formato do nome do pacote e lista de objetos](https://github.com/pure-data/deken/blob/main/developer/README.md)
- [deken: arquivo de configuração](https://github.com/pure-data/deken/blob/main/developer/config.md)
