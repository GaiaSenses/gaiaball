# Como publicar o `[gaiaball]` no Deken

Este guia é para quem mantém o projeto. Ele explica, passo a passo, como colocar uma versão
nova do objeto `[gaiaball]` no Deken, o "gerenciador de pacotes" do Pure Data. Depois de
publicado, qualquer pessoa instala o objeto de dentro do Pd, sem terminal:
**Ajuda → Encontrar externals → "gaiaball" → Instalar**.

> Quem só **usa** o `[gaiaball]` não precisa ler isto: veja o [README](README.md).

O `[gaiaball]` é uma abstração (só patches do Pd vanilla), então o pacote do Deken é **um
só, independente de sistema e de processador**. O CI já o monta a cada versão: aqui, o
trabalho é conferir e enviar.

## 1. Como o Deken funciona, em uma frase

O Deken não é uma loja central: cada autor guarda os seus pacotes na **própria conta do
site [puredata.info](https://puredata.info/)**. A busca do Pd ("Encontrar externals") procura
nos pacotes de todas as contas. Publicar é, portanto, **enviar arquivos para a sua conta no
puredata.info**, com um nome que diz o que tem dentro.

## 2. O que você precisa

| Item | Para quê | Onde |
|---|---|---|
| Conta no puredata.info | É nela que o pacote fica guardado | Seção 3 |
| A ferramenta `deken` | Faz o envio (e, se quiser, monta o pacote em casa) | Seção 4 |
| O pacote da versão (`gaiaball[v<versão>].dek` e os dois arquivos que o acompanham) | É o que vai para o Deken | Seção 5 |

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

Escolha a linha do seu sistema. Para saber qual é o seu Linux: `cat /etc/os-release`.

| Sistema | Como instalar |
|---|---|
| **Ubuntu 24.04** | pelo `pipx`, abaixo |
| **Ubuntu 22.04** | pelo Docker, abaixo (o `deken` exige Python 3.12, e o 22.04 vem com o 3.10) |
| **Ubuntu 25.04 ou mais novo, Debian 13** | `sudo apt install deken`, ou o `pipx` para ter a versão mais nova |
| **Debian 12** | `sudo apt install deken` (versão 0.9.6), ou o Docker para a mais nova (o 12 vem com o Python 3.11) |
| **macOS** | `brew install pipx`, depois `pipx ensurepath` e `pipx install deken`; ou o executável pronto |
| **Windows, fora do WSL** | o executável pronto, abaixo |

> **Por que não `sudo apt-get install deken` no Ubuntu 24.04?** O Ubuntu tem o pacote
> `deken` em quase todas as versões, **menos nas 22.04 e 24.04**, duas versões de suporte
> longo muito usadas no WSL. Nelas o `apt` responde `Unable to locate package deken`.

### Pelo `pipx` (Ubuntu 24.04, inclusive no WSL)

```sh
sudo apt update
sudo apt install pipx
pipx ensurepath
```

Feche e abra o terminal (o `ensurepath` põe `~/.local/bin` no `PATH`). Depois:

```sh
pipx install deken
```

Para atualizar mais tarde: `pipx upgrade deken`. Não use `python3 -m pip install --user …`:
o Ubuntu 24.04 recusa com `externally-managed-environment`.

### Executável pronto (Windows ou macOS)

Na página de [releases do deken](https://github.com/pure-data/deken/releases), baixe
`deken_w64.zip` (Windows) ou `deken_macOS.zip` (macOS) e descompacte.

### Pelo Docker (qualquer sistema com Docker; é o que o CI usa)

Não instala nada além da imagem. No lugar de `deken …`, rode o mesmo comando dentro do
contêiner, a partir da pasta onde estão os arquivos:

```sh
docker pull registry.git.iem.at/pd/deken
docker run --rm -ti --user "$(id -u)" --volume "$PWD":/deken \
    registry.git.iem.at/pd/deken deken --version
```

### Conferir

```sh
deken --version
```

Tem de mostrar um número de versão (por exemplo `0.10.22`).

## 5. Pegar o pacote da versão

O fluxo de trabalho **`pd-gaiaball`** do GitHub Actions monta o pacote do Deken junto com o
`.zip` da página de versões (veja [`PUBLICAR-NO-GITHUB.md`](PUBLICAR-NO-GITHUB.md)). Use o
**da mesma versão** que você publicou no GitHub:

- **Da Release** (o jeito normal): na página da versão, em **Assets**, baixe
  `pd-gaiaball-<versão>-deken.zip` e descompacte. O `.dek` vem dentro de um `.zip` porque o
  GitHub troca os colchetes do nome dos arquivos que recebe, e o Deken precisa deles.

  ```sh
  gh release download v0.1.0 -p 'pd-gaiaball-0.1.0-deken.zip'
  unzip pd-gaiaball-0.1.0-deken.zip -d deken-0.1.0
  ```

- **Do artifact** da execução do CI (`pd-gaiaball-pacote`), se preferir:
  `gh run download <id da execução> -n pd-gaiaball-pacote -D pacote`.

São três arquivos:

| Arquivo | O que é |
|---|---|
| `gaiaball[v0.1.0].dek` | o pacote (um `.zip` com a pasta `gaiaball/`) |
| `gaiaball[v0.1.0].dek.txt` | a lista de objetos, usada pela busca do Pd |
| `gaiaball[v0.1.0].dek.sha256` | a soma de verificação do pacote |

A pasta `gaiaball/` do pacote é a mesma do `.zip` da página de versões, sem o
`LEIA-ME.html`:

```
gaiaball/
├── gaiaball.pd          ← o objeto
├── gaiaball-help.pd     ← a ajuda (o [pd META] tem a descrição e a versão)
├── TUTORIAL.html        ← o tutorial para músicos
├── exemplos/            ← os seis patches do tutorial
└── LICENSE.txt          ← GPL-3.0
```

- **Descrição na busca:** o `deken` monta o `.dek.txt` a partir do `gaiaball-help.pd`: o
  objeto `gaiaball` e o comentário `DESCRIPTION` do subpatch `[pd META]`. O `montar.py` troca
  o `VERSION dev` desse subpatch pelo número da versão.
- **Fontes:** um pacote sem binários não precisa de pacote `(Sources)`. O `deken` só cobra
  fontes de pacotes com binários; este não tem nenhum (o próprio `gaiaball.pd` é a fonte).

### Montar em casa (opcional)

Para conferir antes da tag, ou se o CI não estiver disponível (precisa de `pandoc` 2.19 ou
mais novo, e do `deken` ou do Docker):

```sh
python3 pd-gaiaball/pacotes/montar.py 0.1.0 pacote-0.1.0 --ref v0.1.0
```

## 6. Escolher o número da versão

- Use três números: `0.1.0`, `0.1.1`, `0.2.0`, …
  - Corrigiu um defeito → aumente o último (`0.1.0` → `0.1.1`).
  - Acrescentou algo → aumente o do meio (`0.1.1` → `0.2.0`).
  - Mudou algo de um jeito que quebra patches existentes → aumente o primeiro.
- **Nunca publique duas vezes o mesmo número.** Se errou, publique o número seguinte.
- O número é o da tag do Git (`v0.1.0` → `0.1.0`): o CI tira a versão da tag, e assim se
  sabe de qual código veio o pacote.

## 7. Conferir o nome antes de enviar

O nome **não tem arquitetura**, só o nome e a versão:

```
gaiaball[v0.1.0].dek
```

Se aparecer algo entre parênteses depois da versão (por exemplo `(Linux-amd64-32)`), algum
binário entrou na pasta: **não envie**. O `montar.py` já se recusa a terminar nesse caso.

Confira também a soma:

```sh
sha256sum "gaiaball[v0.1.0].dek"     # tem de ser igual ao conteúdo do .dek.sha256
cat "gaiaball[v0.1.0].dek.txt"       # gaiaball <TAB> a descrição
```

## 8. Enviar

Na pasta onde estão os três arquivos. Eles têm de estar **juntos**: se o `.dek.txt` não
estiver ao lado do `.dek`, o `deken` envia o pacote sem ele, sem avisar, e a busca do Pd fica
sem a descrição.

```sh
DEKEN_USERNAME=<seu-usuário> deken upload --no-sign-gpg "gaiaball[v0.1.0].dek"
```

(use o nome completo do arquivo, entre aspas por causa dos colchetes)

Pelo Docker, o mesmo envio fica assim:

```sh
docker run --rm -ti --user "$(id -u)" --volume "$PWD":/deken \
    --env DEKEN_USERNAME=<seu-usuário> \
    registry.git.iem.at/pd/deken deken upload --no-sign-gpg "gaiaball[v0.1.0].dek"
```

- O `deken` pergunta a **senha** do puredata.info. Sem o `DEKEN_USERNAME=…`, ele pergunta
  também o usuário.
- Ele envia o `.dek` **e** os arquivos auxiliares (`.txt`, `.sha256` e, se houver, `.asc`)
  de uma vez, e termina com `Your upload was successful.`
- O pacote vai para `https://puredata.info/Members/<seu-usuário>/software/gaiaball/0.1.0/`
  (a versão sem o `v`).
- **Assinatura GPG:** é opcional. O `--no-sign-gpg` pula a assinatura. Sem ele, o `deken`
  pede a senha da sua chave GPG, se você tiver uma; se não tiver, avisa
  `Could not GPG sign the package.` e envia assim mesmo.

**Sobre guardar a senha:** depois de um envio que deu certo, o `deken` tenta guardar a senha
no chaveiro do sistema (*keyring*). No WSL normalmente não há chaveiro, e então aparece, logo
depois do `Your upload was successful.`, um aviso `No recommended backend was available…`:
é inofensivo, o envio já terminou. Ele só quer dizer que a senha não foi guardada e será
pedida de novo. O `deken` também aceita um arquivo `~/.config/deken/config` com
`username = …` e `password = …`, mas a senha fica em texto puro no seu computador: prefira
digitá-la a cada envio.

## 9. Conferir que funcionou

1. Num computador **que ainda não tem** o `[gaiaball]`, abra o Pd.
2. **Ajuda → Encontrar externals** (no Pd em inglês: **Help → Find externals**).
3. Procure `gaiaball`. A versão nova deve aparecer, uma só para todos os sistemas. Pode
   levar **até 24 horas** (o próprio `deken` avisa isso no fim do envio): o índice da busca
   é atualizado pelo portal.
4. Instale, crie `[gaiaball]` num patch e abra a ajuda (clique-direito → Ajuda).
5. Conecte a bola pela config-app (ou abra a config-app com `?demo=1`) e confira que os
   `[r gaia.*]` recebem dados.

Faça isso nos três sistemas sempre que possível. O macOS e o Windows são os que menos
testamos.

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
`DEKEN_PASSWORD`. Dá para o GitHub Actions publicar sozinho quando a Release for
publicada, guardando as duas como *secrets* do repositório e rodando a mesma imagem Docker
do `deken`. Só vale a pena quando as versões ficarem frequentes. Até lá, o envio manual
acima é mais seguro: você confere o pacote antes de publicar.

## Referências

- [Documentação do deken](https://deken.readthedocs.io/)
- [deken: guia de linha de comando](https://github.com/pure-data/deken/blob/main/docs/cmdline.md)
- [deken: formato do nome do pacote e lista de objetos](https://github.com/pure-data/deken/blob/main/developer/README.md)
- [deken: arquivo de configuração](https://github.com/pure-data/deken/blob/main/developer/config.md)
