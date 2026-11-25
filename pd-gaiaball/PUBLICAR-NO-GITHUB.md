# Como publicar o `[gaiaball]` na página de versões do GitHub

Este guia é para quem mantém o projeto. Ele explica como colocar uma versão nova do objeto
`[gaiaball]` na **página de versões** (*Releases*) do repositório:
<https://github.com/GaiaSenses/gaiaball/releases>. Lá, cada versão tem um `.zip` por
sistema (Windows, macOS e Linux), e qualquer pessoa baixa sem precisar de conta no GitHub.

Isso não depende do Deken: dá para publicar aqui antes de o `[gaiaball]` estar no Deken, e
continuar publicando aqui depois. Para o Deken, veja [`PUBLICAR-NO-DEKEN.md`](PUBLICAR-NO-DEKEN.md).

> Quem só **usa** o `[gaiaball]` não precisa ler isto: veja o [README](README.md).

## 1. Quem faz o trabalho

O fluxo de trabalho **`pd-gaiaball`** do GitHub Actions (`.github/workflows/pd-gaiaball.yml`).
Ele só roda quando alguém manda, e tem quatro etapas:

| Etapa | O que faz |
|---|---|
| `build` | compila o objeto para Windows, macOS (Intel + Apple Silicon num arquivo só) e Linux (Intel/AMD e ARM), nas duas precisões do Pd, e roda os testes em C. No Mac, assina os binários (assinatura *ad-hoc*: o Apple Silicon só carrega código assinado) |
| `package (Deken)` | monta o pacote do Deken (artifact `gaiaball-deken`) |
| `pacotes` | monta os três `.zip` da página de versões e as notas da versão (artifact `gaiaball-pacotes`), com `pacotes/montar.sh` |
| `release` | **só quando você marca a opção `release`**: cria a Release como **rascunho**, com os três `.zip` e as notas. Ela nunca publica sozinha e se recusa a mexer numa Release já publicada |

Você escolhe a versão, confere o rascunho e publica.

## 2. O que vai em cada pacote

```
pd-gaiaball-<versão>-<sistema>.zip
├── LEIA-ME.html          ← instalação, primeiros passos e problemas, só daquele sistema
└── gaiaball/             ← a pasta que a pessoa instala
    ├── gaiaball.<ext>    ← os binários daquele sistema (Pd comum e de precisão dupla)
    ├── gaiaball-help.pd
    ├── TUTORIAL.html     ← o TUTORIAL.md em HTML
    ├── exemplos/         ← os seis patches
    └── LICENSE.txt
```

- **Windows:** `gaiaball.dll` e `gaiaball.windows-amd64-64.dll`.
- **macOS:** `gaiaball.pd_darwin` e `gaiaball.darwin-fat-64.so`. Nos comentários dos
  patches, o atalho `Ctrl+` vira `Cmd+`.
- **Linux:** `gaiaball.pd_linux`, `gaiaball.linux-amd64-64.so`, `gaiaball.linux-arm64-32.so`,
  `gaiaball.linux-arm64-64.so` e uma cópia do binário ARM com o nome antigo,
  `gaiaball.l_arm64`. O Pd 0.51 a 0.53 num ARM de 64 bits (por exemplo, o Pd do Debian
  num Raspberry Pi) só conhece esse nome.

O `LEIA-ME.html` manda instalar a pasta `gaiaball` na pasta de objetos que o Pd já procura
sozinho (`%AppData%\Pd`, `~/Library/Pd` ou `~/.local/lib/pd/extra`): a pessoa não precisa
mexer em Preferências → Caminho. No Mac, o guia inclui o passo que tira a marca de
"baixado da internet" (`xattr`), sem o qual o macOS bloqueia o objeto.

## 3. Publicar uma versão, pelo terminal

Precisa do [GitHub CLI](https://cli.github.com/) (`gh`), logado numa conta com permissão de
escrita no repositório (`gh auth status` mostra). O código da versão tem que estar na `main`.

1. **Escolha o número** (veja [`PUBLICAR-NO-DEKEN.md`, seção 6](PUBLICAR-NO-DEKEN.md#6-escolher-o-número-da-versão)):
   `0.1.0`, `0.1.1`, `0.2.0`… Nunca reaproveite um número.
2. **Crie e envie a tag:**

   ```sh
   git switch main && git pull
   git tag v0.1.0
   git push origin v0.1.0
   ```

3. **Rode o fluxo de trabalho a partir da tag, com a Release ligada:**

   ```sh
   gh workflow run pd-gaiaball.yml --ref v0.1.0 -f release=true
   gh run watch            # escolha a execução; leva uns 5 minutos
   ```

4. **Confira o rascunho** (seção 6):

   ```sh
   gh release view v0.1.0                       # texto e lista de arquivos
   gh release download v0.1.0 -D conferir-0.1.0 # baixa os três .zip
   ```

   Ou pelo site, em **Releases**: o rascunho aparece só para quem tem acesso ao repositório.
5. **Publique:**

   ```sh
   gh release edit v0.1.0 --draft=false
   ```

As versões `0.x` saem marcadas como **pré-lançamento** (*pre-release*). Quando quiser que
uma versão apareça como a mais recente do repositório:
`gh release edit v1.0.0 --prerelease=false --latest`.

## 4. Publicar uma versão, pelo site

1. Crie e envie a tag pelo terminal (seção 3, passo 2).
2. **Actions → pd-gaiaball → Run workflow**. Em **Use workflow from**, escolha a aba
   **Tags** e a tag da versão. Marque a opção **Criar uma Release** e clique em
   **Run workflow**.
3. Quando terminar, abra **Releases**, confira o rascunho, clique no lápis (**Edit**) e em
   **Publish release**.

## 5. Testar sem criar a versão

Rodando a partir de uma **branch** (e não de uma tag):

- **sem** a opção `release`: os três `.zip` ficam só no artifact `gaiaball-pacotes` da
  execução, com a versão provisória `0.0.0-dev`;
- **com** a opção `release`: além disso, sai um rascunho de teste chamado
  `teste-pd-gaiaball-<número da execução>`. Rascunhos não aparecem para o público e não
  criam tag. Apague quando terminar:

  ```sh
  gh workflow run pd-gaiaball.yml --ref minha-branch -f release=true
  gh release list                                   # ache o teste-pd-gaiaball-N
  gh release delete teste-pd-gaiaball-N --yes
  ```

Para montar os pacotes no seu computador (precisa de `pandoc` 2.19 ou mais novo e `zip`),
com os binários de uma execução do CI:

```sh
gh run download <id da execução> -p 'gaiaball-bin-*' -D bin
mkdir -p binarios && cp bin/*/gaiaball.* binarios/
pd-gaiaball/pacotes/montar.sh 0.0.0-dev binarios pacotes-teste
```

## 6. Conferir antes de publicar

- [ ] A Release tem os três `.zip`, com o número da versão no nome.
- [ ] As notas mostram a versão certa e a soma SHA-256 dos três arquivos.
- [ ] Num computador de verdade (o ideal é um de cada sistema): descompacte, siga o
      `LEIA-ME.html`, crie um `[gaiaball]` (borda contínua) e abra a ajuda.
- [ ] Abra `exemplos/01-ver-tudo.pd` e a config-app com `?demo=1`: os números mudam.
- [ ] No Mac, confira que, sem o passo do `xattr`, aparece o aviso de bloqueio, e que com
      ele o objeto carrega. O macOS é o sistema que menos testamos.

## 7. Corrigir um erro

- **Ainda é rascunho:** rode o fluxo de trabalho de novo a partir da mesma tag (ele troca
  os `.zip` e as notas do rascunho) ou apague o rascunho (`gh release delete v0.1.0 --yes`).
- **Já foi publicada:** não troque os arquivos de uma versão publicada. Quem já baixou
  ficaria com um pacote diferente do que está lá com o mesmo nome. Corrija o código e
  publique o número seguinte. O texto das notas pode ser corrigido a qualquer momento
  (`gh release edit v0.1.0 --notes-file notas.md`).

## 8. Mudar os textos dos pacotes

Tudo fica em `pd-gaiaball/pacotes/`:

| Arquivo | O que é |
|---|---|
| `LEIA-ME.md` | o guia dos três sistemas num arquivo só. Um trecho dentro de `::: so-windows` … `:::`, ou `[texto]{.so-macos}`, só aparece no pacote daquele sistema. `@VERSAO@` e `@REF@` viram a versão e a tag |
| `NOTAS-DA-VERSAO.md` | o texto da Release. `@SOMAS@` vira a lista de somas SHA-256 |
| `estilo.css` | o visual dos HTML (vai embutido em cada arquivo: eles abrem sem internet) |
| `filtro.lua` | o filtro do pandoc que escolhe os trechos de cada sistema e troca links para outros `.md` do repositório por links para o GitHub |
| `montar.sh` | monta os `.zip` e as notas |

O `TUTORIAL.html` sai do `TUTORIAL.md`, sem nenhuma mudança no texto.
