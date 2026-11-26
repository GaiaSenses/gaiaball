#!/usr/bin/env python3
"""Builds the release files of the [gaiaball] abstraction (one package for every system).

    python3 pd-gaiaball/pacotes/montar.py <version> <output dir> [--ref REF] [--deken MODE]

Writes to <output dir>:

  pd-gaiaball-<version>.zip         LEIA-ME.html + the gaiaball/ folder people install
  gaiaball[v<version>].dek          the same gaiaball/ folder as a Deken package (made by
  gaiaball[v<version>].dek.txt      the deken developer tool; no architecture in the name:
  gaiaball[v<version>].dek.sha256   the package holds only Pd patches)
  pd-gaiaball-<version>-deken.zip   those three files, for the GitHub Release (GitHub renames
                                    asset names with brackets, and Deken needs them)
  SHA256SUMS.txt                    sha256 of the .zip and the .dek (sha256sum -c format)
  notas-da-versao.md                the GitHub Release text, with the sums

The gaiaball/ folder: gaiaball.pd, gaiaball-help.pd (META VERSION set to <version>),
exemplos/, TUTORIAL.html (from TUTORIAL.md) and LICENSE.txt.

--ref    tag or commit for the links to GitHub in the HTML and the notes (default: main).
--deken  how to run "deken package": "deken" (on the PATH), "docker" (the image in
         $DEKEN_IMAGE, default registry.git.iem.at/pd/deken), "auto" (deken if found, else
         docker; the default) or "nenhum" (no .dek).

Needs Python 3.8+ and pandoc 2.19+ ($PANDOC overrides the command, e.g. a docker run).
The zip is reproducible: sorted entries, fixed permissions and the date of the last commit
($SOURCE_DATE_EPOCH overrides it). Run by the "pacote" job of
.github/workflows/pd-gaiaball.yml; it also runs at home to check a package before a release.
"""

import argparse
import hashlib
import importlib.util
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path

AQUI = Path(__file__).resolve().parent      # pd-gaiaball/pacotes
PD = AQUI.parent                            # pd-gaiaball
RAIZ = PD.parent                            # repository root
VERSAO_RE = re.compile(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$")


def die(msg):
    sys.exit(f"montar.py: {msg}")


def run(cmd, **kw):
    print("+", " ".join(shlex.quote(str(c)) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def check_patch_is_generated():
    """gaiaball.pd must be exactly what tools/gerar_patch.py writes."""
    spec = importlib.util.spec_from_file_location("gerar_patch", PD / "tools" / "gerar_patch.py")
    gen = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gen)
    want = "\n".join(gen.build_main().render(top=True)) + "\n"
    if (PD / "gaiaball.pd").read_text(encoding="utf-8") != want:
        die("gaiaball.pd differs from what tools/gerar_patch.py generates: "
            "run python3 pd-gaiaball/tools/gerar_patch.py and commit the result")


def pandoc_cmd():
    cmd = shlex.split(os.environ.get("PANDOC", "pandoc"))
    try:
        out = subprocess.run(cmd + ["--version"], check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as e:
        die(f"pandoc not found ({e}); install pandoc 2.19 or newer or set $PANDOC")
    m = re.search(r"pandoc\S* (\d+)\.(\d+)", out.stdout)
    if not m or (int(m.group(1)), int(m.group(2))) < (2, 19):
        die(f"pandoc 2.19 or newer is needed (--embed-resources), got: {out.stdout.splitlines()[0]}")
    return cmd


def html(pandoc, src, dst, title, ref):
    run(pandoc + ["--from", "gfm", "--to", "html5", "--standalone", "--embed-resources",
                  "--css", AQUI / "estilo.css", "--lua-filter", AQUI / "filtro.lua",
                  "--metadata", f"pagetitle={title}", "--metadata", "lang=pt-BR",
                  "--metadata", f"ref={ref}", "--output", dst, src])


def fill(template, **values):
    text = (AQUI / template).read_text(encoding="utf-8")
    for k, v in values.items():
        text = text.replace(f"@{k}@", v)
    left = re.findall(r"@[A-Z]+@", text)
    if left:
        die(f"{template}: placeholders left: {left}")
    return text


def source_date():
    if "SOURCE_DATE_EPOCH" in os.environ:
        return int(os.environ["SOURCE_DATE_EPOCH"])
    try:
        out = subprocess.run(["git", "-C", str(RAIZ), "log", "-1", "--format=%ct"],
                             check=True, capture_output=True, text=True)
        return int(out.stdout.strip())
    except (OSError, subprocess.CalledProcessError, ValueError):
        return int(time.time())


def write_zip(zip_path, base, names, epoch):
    """names: paths relative to base (files and folders), stored sorted with fixed metadata."""
    stamp = time.gmtime(max(epoch, 315532800))[:6]  # zip dates start in 1980
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel in sorted(names):
            p = base / rel
            if p.is_dir():
                info = zipfile.ZipInfo(rel.as_posix() + "/", stamp)
                info.external_attr = (0o40755 << 16) | 0x10
                z.writestr(info, b"")
            else:
                info = zipfile.ZipInfo(rel.as_posix(), stamp)
                info.external_attr = 0o100644 << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, p.read_bytes())


def deken_package(mode, work, versao):
    """Runs "deken package" in work/ (which holds gaiaball/); returns the .dek path."""
    if mode == "auto":
        mode = "deken" if shutil.which("deken") else "docker" if shutil.which("docker") else None
        if mode is None:
            die("neither deken nor docker found: install one, or use --deken nenhum")
    args = ["package", "--version", versao, "gaiaball"]
    if mode == "deken":
        run(["deken"] + args, cwd=work)
    else:
        image = os.environ.get("DEKEN_IMAGE", "registry.git.iem.at/pd/deken")
        user = f"{os.getuid()}:{os.getgid()}" if hasattr(os, "getuid") else None
        run(["docker", "run", "--rm"] + (["--user", user] if user else []) +
            ["-v", f"{work}:/work", "-w", "/work", image, "deken"] + args)
    deks = sorted(work.glob("*.dek"))
    if len(deks) != 1:
        die(f"expected one .dek, found {[d.name for d in deks]}")
    dek = deks[0]
    # Only patches inside: the name must carry no architecture, e.g. gaiaball[v0.1.0].dek
    if dek.name != f"gaiaball[v{versao}].dek":
        die(f"unexpected Deken package name {dek.name!r} (an architecture in the name means a "
            "binary got into the folder)")
    for ext in (".txt", ".sha256"):
        if not Path(str(dek) + ext).is_file():
            die(f"deken did not write {dek.name}{ext}")
    return dek


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("versao", help="e.g. 0.1.0 (goes into the file names and the texts)")
    ap.add_argument("saida", help="output folder (created if missing)")
    ap.add_argument("--ref", default="main", help="tag or commit for the GitHub links")
    ap.add_argument("--deken", default="auto", choices=["auto", "deken", "docker", "nenhum"])
    a = ap.parse_args()
    if not VERSAO_RE.match(a.versao):
        die(f"version {a.versao!r} is not X.Y.Z or X.Y.Z-suffix")

    check_patch_is_generated()
    pandoc = pandoc_cmd()
    saida = Path(a.saida).resolve()
    saida.mkdir(parents=True, exist_ok=True)
    epoch = source_date()

    with tempfile.TemporaryDirectory(prefix="gaiaball-pacote-") as tmp:
        tmp = Path(tmp)
        raiz_zip = tmp / "zip"
        pasta = raiz_zip / "gaiaball"
        pasta.mkdir(parents=True)

        shutil.copyfile(PD / "gaiaball.pd", pasta / "gaiaball.pd")
        ajuda = (PD / "gaiaball-help.pd").read_text(encoding="utf-8")
        ajuda, n = re.subn(r"^(#X text \d+ \d+ VERSION )\S+;$", rf"\g<1>{a.versao};", ajuda,
                           flags=re.M)
        if n != 1:
            die(f"gaiaball-help.pd: expected one 'VERSION' line in [pd META], found {n}")
        (pasta / "gaiaball-help.pd").write_text(ajuda, encoding="utf-8")
        shutil.copytree(PD / "exemplos", pasta / "exemplos")
        shutil.copyfile(RAIZ / "LICENSE", pasta / "LICENSE.txt")
        html(pandoc, PD / "TUTORIAL.md", pasta / "TUTORIAL.html", "Tutorial do [gaiaball]", a.ref)

        leia = tmp / "LEIA-ME.md"
        leia.write_text(fill("LEIA-ME.md", VERSAO=a.versao, REF=a.ref), encoding="utf-8")
        html(pandoc, leia, raiz_zip / "LEIA-ME.html", f"[gaiaball] {a.versao}: leia-me", a.ref)

        nomes = [p.relative_to(raiz_zip) for p in raiz_zip.rglob("*")]
        zip_path = saida / f"pd-gaiaball-{a.versao}.zip"
        write_zip(zip_path, raiz_zip, nomes, epoch)
        print(f"pronto: {zip_path}")
        arquivos = [zip_path]

        if a.deken != "nenhum":
            work = tmp / "deken"
            shutil.copytree(pasta, work / "gaiaball")
            dek = deken_package(a.deken, work, a.versao)
            trio = [dek, Path(str(dek) + ".txt"), Path(str(dek) + ".sha256")]
            for f in trio:
                shutil.copyfile(f, saida / f.name)
                print(f"pronto: {saida / f.name}")
            arquivos.append(saida / dek.name)
            dek_sum = (saida / (dek.name + ".sha256")).read_text().split()[0]
            if dek_sum != sha256(saida / dek.name):
                die("the .dek.sha256 written by deken does not match the .dek")
            bundle = saida / f"pd-gaiaball-{a.versao}-deken.zip"
            write_zip(bundle, work, [f.relative_to(work) for f in trio], epoch)
            print(f"pronto: {bundle}")

    somas = {f.name: sha256(f) for f in arquivos}
    (saida / "SHA256SUMS.txt").write_text(
        "".join(f"{h}  {n}\n" for n, h in somas.items()), encoding="utf-8")
    lista = "\n".join(f"- `{n}`: `{h}`" for n, h in somas.items())
    (saida / "notas-da-versao.md").write_text(
        fill("NOTAS-DA-VERSAO.md", VERSAO=a.versao, REF=a.ref, SOMAS=lista), encoding="utf-8")
    print(f"pronto: {saida / 'SHA256SUMS.txt'}")
    print(f"pronto: {saida / 'notas-da-versao.md'}")


if __name__ == "__main__":
    main()
