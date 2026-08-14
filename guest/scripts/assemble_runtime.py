"""Assembla una directory di runtime autonoma con QEMU e le sue dipendenze.

Perche' serve. QEMU importa `libvirglrenderer-1.dll` per nome e Windows la
cerca **nella directory dell'eseguibile** prima che nel PATH: non c'e' modo
di sostituire il ponte con una variabile d'ambiente. Durante le sonde la si
e' copiata dentro `C:\\msys64\\clangarm64\\bin`, cioe' modificando
un'installazione gestita da pacman. Va bene per una prova, non per il
progetto: al primo `pacman -Syu` la modifica sparisce, e nel frattempo ogni
altro programma che usa quel QEMU vede il nostro ponte.

Qui si copia QEMU e l'intera chiusura delle sue dipendenze in una directory
di nostra proprieta', e dentro quella si mette il nostro
`libvirglrenderer-1.dll`. MSYS2 resta intatto.

La chiusura delle dipendenze si calcola leggendo la tabella di import di
ogni PE con `objdump -p` e seguendola in ampiezza. Si copiano solo le DLL
che stanno in `clangarm64/bin`: tutto il resto — `KERNEL32`, `d3d11`,
`api-ms-win-crt-*` — appartiene al sistema e non va ridistribuito.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from collections import deque
from pathlib import Path

DLL_NAME = re.compile(r"DLL Name:\s*(\S+)")

# Caricate a runtime con LoadLibrary, quindi invisibili a objdump: nessun PE
# le nomina nella tabella di import, ma senza di esse QEMU muore con un
# segfault appena `-display egl-headless` prova a creare un contesto.
#
# libepoxy risolve EGL e GLES per nome a runtime. Su questa piattaforma le
# fornisce **ANGLE** (pacchetto mingw-w64-clang-aarch64-angleproject), che
# traduce GLES in D3D11: e' il motivo per cui il GL dell'host e' accelerato
# sull'Adreno senza che serva la Mesa d3d12 di research/mesa.
RUNTIME_LOADED = [
    "libEGL.dll",
    "libGLESv2.dll",
]


def imports_of(objdump: Path, pe: Path) -> list[str]:
    out = subprocess.run([str(objdump), "-p", str(pe)],
                         capture_output=True, text=True, errors="replace")
    if out.returncode != 0:
        raise SystemExit(f"objdump ha fallito su {pe}:\n{out.stderr[-800:]}")
    return DLL_NAME.findall(out.stdout)


def collect(objdump: Path, roots: list[Path], search: Path) -> list[Path]:
    """Chiusura transitiva delle dipendenze presenti in `search`."""
    # I nomi nella tabella di import non hanno un case garantito
    # (`MFPlat.DLL`, `KERNEL32.dll`): l'indice va costruito in minuscolo,
    # altrimenti una dipendenza reale sembra di sistema e non viene copiata.
    available = {p.name.lower(): p for p in search.glob("*.dll")}

    found: dict[str, Path] = {}
    queue: deque[Path] = deque(roots)
    seen: set[str] = set()

    while queue:
        pe = queue.popleft()
        for name in imports_of(objdump, pe):
            key = name.lower()
            if key in seen:
                continue
            seen.add(key)
            dep = available.get(key)
            if dep is None:
                continue          # DLL di sistema: non si ridistribuisce
            found[key] = dep
            queue.append(dep)

    return sorted(found.values())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--msys", type=Path, default=Path(r"C:\msys64"))
    ap.add_argument("--bridge", type=Path, required=True,
                    help="libvirglrenderer-1.dll costruita da noi")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--target", default="qemu-system-aarch64.exe")
    ap.add_argument("--also", nargs="*", default=["qemu-img.exe"],
                    help="altri eseguibili da includere, se presenti")
    ap.add_argument(
        "--qemu-build", type=Path, default=None,
        help="directory ninja del nostro QEMU compilato da sorgente "
             "(qemu/ui-winq innestato), es. "
             r"C:\msys64\home\<utente>\qemu-11.0.3\build. Se --target vi si "
             "trova, si usa quello al posto del qemu-system-aarch64.exe di "
             "pacman: e' l'unico che conosce -display winq. Default: "
             r"<msys>\home\%%USERNAME%%\qemu-11.0.3\build, perche' MSYS2 "
             "nomina la home dell'utente come l'account Windows.")
    ap.add_argument("--vulkan-icd", type=Path, default=None,
                    help="directory con l'ICD Vulkan da includere "
                         "(manifest .json piu' la sua DLL)")
    args = ap.parse_args()

    bin_dir = args.msys / "clangarm64" / "bin"
    objdump = bin_dir / "objdump.exe"
    for p in (bin_dir, objdump):
        if not p.exists():
            raise SystemExit(f"non trovato: {p}")

    # Quale qemu-system-aarch64.exe entra nel runtime: il nostro (compilato da
    # sorgente, con qemu/ui-winq innestato e -display winq) se c'e', quello
    # di pacman altrimenti. Pacman non lo sa costruire con winq -- non e' nel
    # suo albero -- quindi usarlo senza controllo lascerebbe il runtime senza
    # il backend che questo progetto esiste per avere, e in modo silenzioso:
    # `qemu-system-aarch64.exe --version` risponderebbe comunque, solo senza
    # "-display winq" fra le opzioni. Da qui la riga di log qui sotto: sapere
    # quale dei due si sta impacchettando evita mezz'ora di confusione a chi
    # poi si chiede perche' -display winq non parte dal runtime assemblato.
    qemu_build = args.qemu_build
    if qemu_build is None:
        utente = os.environ.get("USERNAME", "")
        qemu_build = args.msys / "home" / utente / "qemu-11.0.3" / "build"

    nostro = qemu_build / args.target
    pacman = bin_dir / args.target
    if nostro.exists():
        exes = [nostro]
        print(f"qemu binario: IL NOSTRO ({nostro}), compilato da sorgente")
    elif pacman.exists():
        exes = [pacman]
        print(f"qemu binario: quello di PACMAN ({pacman}) -- "
              f"il nostro non e' stato trovato in {qemu_build}. "
              f"Questo runtime NON avra' -display winq.")
    else:
        raise SystemExit(
            f"nessun {args.target} trovato ne' in {qemu_build} "
            f"ne' in {bin_dir} (manca mingw-w64-clang-aarch64-qemu?)")

    for name in args.also:
        cand = bin_dir / name
        if cand.exists():
            exes.append(cand)

    if not args.bridge.exists():
        raise SystemExit(f"ponte non trovato: {args.bridge}\n"
                         f"Eseguire prima research/scripts/build-virgl.ps1")

    # Le DLL caricate a runtime entrano fra le radici della ricerca, non fra
    # i risultati: anche le *loro* dipendenze servono.
    extra_roots = []
    for name in RUNTIME_LOADED:
        cand = bin_dir / name
        if cand.exists():
            extra_roots.append(cand)
        else:
            print(f"ATTENZIONE: {name} assente in {bin_dir}. "
                  f"Serve mingw-w64-clang-aarch64-angleproject.")

    deps = collect(objdump, exes + extra_roots, bin_dir)
    # collect() ritorna solo le dipendenze; le radici aggiuntive vanno
    # aggiunte a mano, altrimenti si copierebbero i loro import ma non loro.
    deps = sorted(set(deps) | set(extra_roots))

    out_bin = args.out / "bin"
    out_bin.mkdir(parents=True, exist_ok=True)
    for src in exes + deps:
        shutil.copy2(src, out_bin / src.name)

    # Il nostro ponte va copiato *dopo*: fra le dipendenze c'e' anche la
    # libvirglrenderer di MSYS2, e senza Venus. Questa la sovrascrive.
    shutil.copy2(args.bridge, out_bin / "libvirglrenderer-1.dll")

    # I dati di QEMU: firmware, keymap, ROM di opzione. Senza questi
    # `-bios edk2-aarch64-code.fd` non si risolve.
    share_src = args.msys / "clangarm64" / "share" / "qemu"
    share_dst = args.out / "share" / "qemu"
    if share_src.is_dir():
        if share_dst.exists():
            shutil.rmtree(share_dst)
        shutil.copytree(share_src, share_dst)

    # Il driver Vulkan dell'host. Serve al ponte per Venus, e su questa
    # macchina non ce n'e' alcuno registrato nel sistema: la chiave
    # HKLM\SOFTWARE\Khronos\Vulkan\Drivers e' vuota. Senza ICD il loader non
    # trova nessun device e Venus non ha su cosa appoggiarsi, quindi va
    # ridistribuito insieme al resto e indicato con VK_DRIVER_FILES.
    icd_note = "nessuno"
    if args.vulkan_icd and args.vulkan_icd.is_dir():
        icd_dst = args.out / "vulkan"
        if icd_dst.exists():
            shutil.rmtree(icd_dst)
        shutil.copytree(args.vulkan_icd, icd_dst)
        manifests = sorted(icd_dst.glob("*.json"))
        icd_note = ", ".join(m.name for m in manifests) if manifests else \
                   "ATTENZIONE: nessun manifest .json nella directory"

    total = sum(f.stat().st_size for f in args.out.rglob("*") if f.is_file())
    print(f"eseguibili : {len(exes)}")
    print(f"dipendenze : {len(deps)} DLL da clangarm64/bin")
    print(f"ponte      : {args.bridge}")
    print(f"dati qemu  : {'copiati' if share_src.is_dir() else 'ASSENTI'}")
    print(f"ICD Vulkan : {icd_note}")
    print(f"totale     : {total / 1e6:.1f} MB in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
