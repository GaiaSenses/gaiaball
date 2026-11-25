#!/usr/bin/env bash
# Monta os pacotes de instalação do [gaiaball] para a página de versões (Releases) do
# GitHub: um .zip por sistema (Windows, macOS, Linux), cada um com o objeto compilado, o
# LEIA-ME.html daquele sistema, o tutorial em HTML, o patch de ajuda e os exemplos. Gera
# também as notas da versão (notas-da-versao.md), com a soma SHA-256 de cada .zip.
#
# Uso:  pd-gaiaball/pacotes/montar.sh <versão> <pasta dos binários> <pasta de saída> [ref]
#
#   <versão>             ex.: 0.1.0 (vai no nome dos arquivos e nos textos)
#   <pasta dos binários> os oito binários que o CI gera (gaiaball.dll, gaiaball.pd_darwin...)
#   <pasta de saída>     onde gravar os .zip e o notas-da-versao.md (é criada se faltar)
#   [ref]                tag ou branch dos links para o código no GitHub (padrão: main)
#
# Precisa de: bash 4+, pandoc 2.19+ (--embed-resources), zip e sha256sum ou shasum.
# Quem roda: o job "pacotes" de .github/workflows/pd-gaiaball.yml. Dá para rodar em casa
# também, para conferir os pacotes antes de publicar.
set -euo pipefail

if [ $# -lt 3 ] || [ $# -gt 4 ]; then
  sed -n '7,12p' "$0" >&2
  exit 2
fi

VERSAO=$1
BIN=$(cd "$2" && pwd)
mkdir -p "$3"
SAIDA=$(cd "$3" && pwd)
REF=${4:-main}
AQUI=$(cd "$(dirname "$0")" && pwd)
PD=$(dirname "$AQUI")         # pd-gaiaball/
RAIZ=$(dirname "$PD")         # raiz do repositório

# Os binários de cada sistema (os nomes são as extensões que o CI usa; ver pd-gaiaball.yml).
declare -A BINARIOS=(
  [windows]="gaiaball.dll gaiaball.windows-amd64-64.dll"
  [macos]="gaiaball.pd_darwin gaiaball.darwin-fat-64.so"
  [linux]="gaiaball.pd_linux gaiaball.linux-amd64-64.so gaiaball.linux-arm64-32.so gaiaball.linux-arm64-64.so"
)
NOME_DO_SISTEMA_windows="Windows"
NOME_DO_SISTEMA_macos="macOS"
NOME_DO_SISTEMA_linux="Linux"

for so in windows macos linux; do
  for b in ${BINARIOS[$so]}; do
    [ -f "$BIN/$b" ] || { echo "falta o binário $BIN/$b" >&2; exit 1; }
  done
done

if command -v sha256sum >/dev/null; then SHA=(sha256sum); else SHA=(shasum -a 256); fi

trabalho=$(mktemp -d)
trap 'rm -rf "$trabalho"' EXIT

# pandoc: GFM (as âncoras dos títulos são as mesmas do GitHub, então os links do
# Sumário funcionam), com blocos ::: e trechos [..]{.classe} para o LEIA-ME.
html() { # html <entrada.md> <saída.html> <título da aba> <sistema ou "">
  pandoc --from gfm+fenced_divs+bracketed_spans --to html5 --standalone --embed-resources \
    --css "$AQUI/estilo.css" --lua-filter "$AQUI/filtro.lua" \
    --metadata pagetitle="$3" --metadata lang=pt-BR --metadata ref="$REF" \
    ${4:+--metadata so="$4"} \
    --output "$2" "$1"
}

html "$PD/TUTORIAL.md" "$trabalho/TUTORIAL.html" "Tutorial do [gaiaball]" ""

for so in windows macos linux; do
  pacote="$trabalho/$so"
  dest="$pacote/gaiaball"
  mkdir -p "$dest"

  for b in ${BINARIOS[$so]}; do cp "$BIN/$b" "$dest/"; done
  if [ "$so" = linux ]; then
    # O Pd 0.51 a 0.53 num ARM de 64 bits não conhece o nome novo (.linux-arm64-32.so) e
    # tentaria o .pd_linux, que é o binário Intel/AMD. O nome antigo do ARM resolve.
    cp "$BIN/gaiaball.linux-arm64-32.so" "$dest/gaiaball.l_arm64"
  fi

  cp "$PD/gaiaball-help.pd" "$dest/"
  cp -R "$PD/exemplos" "$dest/exemplos"
  cp "$RAIZ/LICENSE" "$dest/LICENSE.txt"
  cp "$trabalho/TUTORIAL.html" "$dest/TUTORIAL.html"

  if [ "$so" = macos ]; then
    # Nos comentários dos patches, o atalho do Mac é Cmd, não Ctrl.
    for f in "$dest"/exemplos/*.pd "$dest/gaiaball-help.pd"; do
      sed 's|Ctrl+|Cmd+|g' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
    done
  fi

  nome="NOME_DO_SISTEMA_$so"
  sed -e "s|@VERSAO@|$VERSAO|g" -e "s|@REF@|$REF|g" "$AQUI/LEIA-ME.md" > "$trabalho/LEIA-ME-$so.md"
  html "$trabalho/LEIA-ME-$so.md" "$pacote/LEIA-ME.html" "[gaiaball] $VERSAO para ${!nome}: leia-me" "$so"

  zipfile="$SAIDA/pd-gaiaball-$VERSAO-$so.zip"
  rm -f "$zipfile"
  (cd "$pacote" && zip -q -r -X "$zipfile" LEIA-ME.html gaiaball)
  echo "pronto: $zipfile"
done

# Notas da versão, com a soma de cada .zip.
somas=$(cd "$SAIDA" && for so in windows macos linux; do
  f="pd-gaiaball-$VERSAO-$so.zip"
  printf -- '- `%s`: `%s`\n' "$f" "$("${SHA[@]}" "$f" | cut -d' ' -f1)"
done)
SOMAS="$somas" awk '{ if ($0 == "@SOMAS@") print ENVIRON["SOMAS"]; else print }' "$AQUI/NOTAS-DA-VERSAO.md" |
  sed -e "s|@VERSAO@|$VERSAO|g" -e "s|@REF@|$REF|g" > "$SAIDA/notas-da-versao.md"
echo "pronto: $SAIDA/notas-da-versao.md"
