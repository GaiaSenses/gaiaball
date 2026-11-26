# Como publicar o `[gaiaball]` na página de versões do GitHub

Este guia é para quem mantém o projeto. Ele explica como colocar uma versão nova do objeto
`[gaiaball]` na **página de versões** (*Releases*) do repositório:
<https://github.com/GaiaSenses/gaiaball/releases>. Lá, cada versão tem **um `.zip` só**, que
serve para Windows, macOS e Linux (o `[gaiaball]` é uma abstração, sem nada compilado), e
qualquer pessoa baixa sem precisar de conta no GitHub.

Isso não depende do Deken: dá para publicar aqui antes de o `[gaiaball]` estar no Deken, e
continuar publicando aqui depois. Para o Deken, veja [`PUBLICAR-NO-DEKEN.md`](PUBLICAR-NO-DEKEN.md).

> Quem só **usa** o `[gaiaball]` não precisa ler isto: veja o [README](README.md).

## 1. Quem faz o trabalho

O fluxo de trabalho **`pd-gaiaball`** do GitHub Actions (`.github/workflows/pd-gaiaball.yml`):

| Etapa | Quando roda | O que faz |
|---|---|---|
| `teste` | todo push na `main` e todo pull request que mexe em `pd-gaiaball/`, toda tag `v*` e à mão | roda o gerador e falha se o `gaiaball.pd` mudar num byte; roda o `e2e.mjs` (patch aberto direto e `[gaiaball]` como objeto) e o `ajuda.mjs` num Pd de verdade (o `puredata` do Ubuntu) |
| `pacote` | depois do `teste` | monta o `.zip`, o pacote do Deken, as somas e as notas com `pacotes/montar.py`; instala o `.zip` e o `.dek` em pastas novas e roda o `e2e.mjs` contra cada um; guarda tudo no artifact `pd-gaiaball-pacote` |
| `release` | **só numa tag `v*` enviada** | cria a Release como **rascunho**, com os arquivos e as notas. Ela nunca publica sozinha e se recusa a mexer numa Release já publicada |

Você escolhe a versão, confere o rascunho e publica. Rodando à mão (**Run workflow**), o
fluxo monta os arquivos e **nunca** cria Release.

## 2. O que vai na Release

| Arquivo | Para quem |
|---|---|
| `pd-gaiaball-<versão>.zip` | quem instala à mão, em qualquer sistema |
| `pd-gaiaball-<versão>-deken.zip` | a equipe: o `gaiaball[v<versão>].dek` (com o `.dek.txt` e o `.dek.sha256`) para enviar ao Deken |
| `SHA256SUMS.txt` | as somas do `.zip` e do `.dek` (`sha256sum -c SHA256SUMS.txt`) |

O texto da Release (as notas) diz qual arquivo baixar e traz as mesmas somas.

O `.zip` tem:

```
pd-gaiaball-<versão>.zip
├── LEIA-ME.html          ← instalação no Windows, no macOS e no Linux, primeiros passos e problemas
└── gaiaball/             ← a pasta que a pessoa instala
    ├── gaiaball.pd
    ├── gaiaball-help.pd  ← com a versão no [pd META]
    ├── TUTORIAL.html     ← o TUTORIAL.md em HTML
    ├── exemplos/         ← os seis patches
    └── LICENSE.txt
```

O `LEIA-ME.html` manda pôr a pasta `gaiaball` numa pasta que o Pd já procura sozinho
(`%AppData%\Pd`, `~/Library/Pd` ou `~/.local/lib/pd/extra`): a pessoa não precisa mexer em
Preferências → Caminho. No Mac não há passo de liberar nada, porque não há binário. O CI
confere o caminho do Linux: instala o `.zip` num `~/.local/lib/pd/extra` vazio e roda o
teste ponta a ponta com um `[gaiaball]` achado só pelos caminhos padrão.

## 3. Publicar uma versão, pelo terminal

Precisa do [GitHub CLI](https://cli.github.com/) (`gh`), logado numa conta com permissão de
escrita no repositório (`gh auth status` mostra). O código da versão tem que estar na `main`.

1. **Escolha o número** (veja [`PUBLICAR-NO-DEKEN.md`, seção 6](PUBLICAR-NO-DEKEN.md#6-escolher-o-número-da-versão)):
   `0.1.0`, `0.1.1`, `0.2.0`… Nunca reaproveite um número.
2. **Crie e envie a tag.** É isso que dispara o fluxo de trabalho com a etapa `release`:

   ```sh
   git switch main && git pull
   git tag v0.1.0
   git push origin v0.1.0
   ```

3. **Acompanhe** (leva uns 3 minutos):

   ```sh
   gh run list --workflow pd-gaiaball.yml --limit 3
   gh run watch <id da execução>
   ```

4. **Confira o rascunho** (seção 6):

   ```sh
   gh release view v0.1.0                       # texto e lista de arquivos
   gh release download v0.1.0 -D conferir-0.1.0 # baixa os arquivos
   (cd conferir-0.1.0 && unzip -o pd-gaiaball-0.1.0-deken.zip && sha256sum -c SHA256SUMS.txt)
   ```

   Ou pelo site, em **Releases**: o rascunho aparece só para quem tem acesso ao repositório.
5. **Publique:**

   ```sh
   gh release edit v0.1.0 --draft=false
   ```

6. **Envie ao Deken** o `.dek` da mesma versão: [`PUBLICAR-NO-DEKEN.md`](PUBLICAR-NO-DEKEN.md).

As versões `0.x` (e as com sufixo, como `0.2.0-rc.1`) saem marcadas como
**pré-lançamento** (*pre-release*). Quando quiser que uma versão apareça como a mais recente
do repositório: `gh release edit v1.0.0 --prerelease=false --latest`.

## 4. Publicar uma versão, pelo site

1. Crie e envie a tag pelo terminal (seção 3, passo 2). Pelo site, dá para criar a tag em
   **Releases → Draft a new release → Choose a tag**, mas aí é o site que cria a Release:
   prefira a tag pelo terminal e deixe o CI montar o rascunho.
2. Em **Actions → pd-gaiaball**, espere a execução da tag terminar.
3. Abra **Releases**, confira o rascunho, clique no lápis (**Edit**) e em
   **Publish release**.

## 5. Testar sem criar a versão

- **Num pull request**, o fluxo já roda o `teste` e o `pacote`: os arquivos (com a versão
  provisória `0.0.0-dev`) ficam no artifact `pd-gaiaball-pacote` da execução.
- **À mão:** **Actions → pd-gaiaball → Run workflow**, a partir de qualquer branch. Monta
  os mesmos arquivos e não cria Release:

  ```sh
  gh workflow run pd-gaiaball.yml --ref minha-branch
  gh run download <id da execução> -n pd-gaiaball-pacote -D pacote-teste
  ```

- **No seu computador** (precisa de `pandoc` 2.19 ou mais novo, e do `deken` ou do Docker;
  sem nenhum dos dois, use `--deken nenhum`):

  ```sh
  python3 pd-gaiaball/pacotes/montar.py 0.0.0-dev pacote-teste
  ```

  Sem `pandoc` instalado, dá para usar a imagem Docker dele:
  `PANDOC="docker run --rm --user $(id -u):$(id -g) -v $PWD:$PWD -v /tmp:/tmp pandoc/core:3.1.1"`
  (rode da raiz do repositório).

## 6. Conferir antes de publicar

- [ ] A Release tem o `.zip`, o `-deken.zip` e o `SHA256SUMS.txt`, com o número da versão no
      nome.
- [ ] As notas mostram a versão certa e as somas SHA-256.
- [ ] A execução do CI passou nas três etapas (o `pacote` já instalou o `.zip` e o `.dek`
      e rodou o teste ponta a ponta contra eles).
- [ ] Num computador de verdade (o ideal é um de cada sistema): descompacte, siga o
      `LEIA-ME.html`, crie um `[gaiaball]` (borda contínua) e abra a ajuda.
- [ ] Abra `exemplos/01-ver-tudo.pd` e a config-app com `?demo=1`: os números mudam.
- [ ] No Windows e no macOS, confira a pasta do `LEIA-ME.html` (`%AppData%\Pd`,
      `~/Library/Pd`): o CI só testa a do Linux.

## 7. Corrigir um erro

- **Ainda é rascunho:** rode de novo a execução da tag (**Re-run all jobs**, ou
  `gh run rerun <id>`): ela troca os arquivos e as notas do rascunho. Se o código precisa
  mudar, apague o rascunho (`gh release delete v0.1.0 --yes`) e a tag
  (`git push --delete origin v0.1.0 && git tag -d v0.1.0`), corrija na `main` e crie a tag
  de novo.
- **Já foi publicada:** não troque os arquivos de uma versão publicada (o fluxo se recusa).
  Quem já baixou ficaria com um pacote diferente do que está lá com o mesmo nome. Corrija o
  código e publique o número seguinte. O texto das notas pode ser corrigido a qualquer
  momento (`gh release edit v0.1.0 --notes-file notas.md`).

## 8. Mudar os textos do pacote

Tudo fica em `pd-gaiaball/pacotes/`:

| Arquivo | O que é |
|---|---|
| `LEIA-ME.md` | o guia de instalação dos três sistemas. `@VERSAO@` e `@REF@` viram a versão e a tag |
| `NOTAS-DA-VERSAO.md` | o texto da Release. `@SOMAS@` vira a lista de somas SHA-256 |
| `estilo.css` | o visual dos HTML (vai embutido em cada arquivo: eles abrem sem internet) |
| `filtro.lua` | o filtro do pandoc que troca links para outros `.md` do repositório por links para o GitHub |
| `montar.py` | monta o `.zip`, o pacote do Deken, as somas e as notas |

O `TUTORIAL.html` sai do `TUTORIAL.md`, sem nenhuma mudança no texto. O `montar.py` se
recusa a empacotar um `gaiaball.pd` diferente do que o gerador produz.
