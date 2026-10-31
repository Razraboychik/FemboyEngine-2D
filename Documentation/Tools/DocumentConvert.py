#!/usr/bin/env python3
"""Converts the generated PDF editions to DjVu.

DjVu is the fourth documentation format. It stores a document as compressed
layers and reads well in a dedicated viewer. The preferred converter is
`pdf2djvu` from DjVuLibre. When it is unavailable, the module rasterises the PDF
with `pdftoppm` and packages the pages with DjVuLibre's `c44` and `djvm`. Both
paths are documentation tooling for the publishing machine, never an engine
dependency: nothing links this module, CMake does not see it, and a machine that
only builds the engine never needs it.

  * the converter is `FBY_PDF2DJVU` when set, otherwise `pdf2djvu` on PATH;
  * the raster fallback uses `FBY_PDFTOPPM`, `FBY_C44` and `FBY_DJVM`, or the
    same tools on PATH;
  * the background is rasterised at `FBY_PDF2DJVU_DPI` (150 by default);
  * when neither path is available the export is skipped with one line and never
    fails the run, because a machine without DjVuLibre must still build the rest
    of the set;
  * a produced file is verified by its `AT&TFORM` magic and a non-zero size.

Reading them: Okular (KDE) opens DjVu natively, as does Evince with the DjVu
plugin, WinDjView on Windows and the DjVu browser plug-in.

Usage:
  python Documentation/Tools/DocumentConvert.py --pdf <file.pdf> --djvu <file.djvu>
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

# A DjVu file starts with the IFF form type for a single-page document.
DJVU_MAGIC = b"AT&TFORM"

# Background resolution. The text layer is a separate JB2 mask and stays sharp at
# any zoom, so the background only carries fills, rules and images: 150 dpi keeps
# it readable and about 40% smaller than the 300 dpi default of pdf2djvu.
DEFAULT_DPI = os.environ.get("FBY_PDF2DJVU_DPI", "150")


def FindPdf2Djvu() -> str | None:
    """Returns the converter path, or None when DjVuLibre is not installed."""
    configured = os.environ.get("FBY_PDF2DJVU", "")
    if configured:
        candidate = pathlib.Path(configured)
        return str(candidate) if candidate.is_file() else None
    return shutil.which("pdf2djvu")


def FindTool(variable: str, name: str) -> str | None:
    """Returns an explicitly configured tool or one found on PATH."""
    configured = os.environ.get(variable, "")
    if configured:
        candidate = pathlib.Path(configured)
        return str(candidate) if candidate.is_file() else None
    return shutil.which(name)


def HasDjvuMagic(path: pathlib.Path) -> bool:
    """Returns true when a non-empty file starts with the DjVu IFF form."""
    if not path.is_file() or path.stat().st_size == 0:
        return False
    with path.open("rb") as handle:
        return handle.read(len(DJVU_MAGIC)) == DJVU_MAGIC


def BuildRasterDjvu(pdf_path: pathlib.Path, djvu_path: pathlib.Path) -> bool:
    """Builds a page-image DjVu when pdf2djvu is unavailable."""
    pdftoppm = FindTool("FBY_PDFTOPPM", "pdftoppm")
    c44 = FindTool("FBY_C44", "c44")
    djvm = FindTool("FBY_DJVM", "djvm")
    if not (pdftoppm and c44 and djvm):
        return False

    with tempfile.TemporaryDirectory(prefix="fby_djvu_") as temporary:
        work = pathlib.Path(temporary)
        prefix = work / "page"
        raster = subprocess.run(
            [pdftoppm, "-r", DEFAULT_DPI, "-jpeg", "-jpegopt", "quality=88", str(pdf_path), str(prefix)],
            check=False,
            capture_output=True,
        )
        if raster.returncode != 0:
            print(f"djvu raster failed: {raster.stderr.decode('utf-8', errors='replace').strip()[:400]}")
            return False
        pages = sorted(work.glob("page-*.jpg"), key=lambda item: int(item.stem.rsplit("-", 1)[-1]))
        if not pages:
            print("djvu raster failed: pdftoppm produced no pages")
            return False
        encoded: list[str] = []
        for index, page in enumerate(pages):
            page_djvu = work / f"page-{index:04d}.djvu"
            result = subprocess.run(
                [c44, "-dpi", DEFAULT_DPI, str(page), str(page_djvu)],
                check=False,
                capture_output=True,
            )
            if result.returncode != 0 or not page_djvu.is_file():
                print(f"djvu raster failed: c44: {result.stderr.decode('utf-8', errors='replace').strip()[:400]}")
                return False
            encoded.append(str(page_djvu))
        djvu_path.unlink(missing_ok=True)
        result = subprocess.run(
            [djvm, "-c", str(djvu_path), *encoded],
            check=False,
            capture_output=True,
        )
        if result.returncode != 0:
            print(f"djvu raster failed: djvm: {result.stderr.decode('utf-8', errors='replace').strip()[:400]}")
            return False
    return HasDjvuMagic(djvu_path)


def BuildDjvu(pdf_path: pathlib.Path, djvu_path: pathlib.Path) -> bool:
    """Converts one PDF to DjVu. Prints its own status line, never raises."""
    if not pdf_path.is_file() or pdf_path.stat().st_size == 0:
        print(f"djvu skipped: no PDF to convert ({pdf_path.name})")
        return False

    djvu_path.parent.mkdir(parents=True, exist_ok=True)
    converter = FindPdf2Djvu()
    if converter is None:
        if not BuildRasterDjvu(pdf_path, djvu_path):
            print(f"djvu skipped: install pdf2djvu or set FBY_PDFTOPPM, FBY_C44 and FBY_DJVM ({djvu_path.name})")
            return False
    else:
        if djvu_path.is_file():
            djvu_path.unlink()
        command = [converter, "--no-metadata", f"--dpi={DEFAULT_DPI}", "-o", str(djvu_path), str(pdf_path)]
        result = subprocess.run(command, check=False, capture_output=True)
        if result.returncode != 0:
            print(f"djvu export failed: {pdf_path.name}")
            if result.stderr:
                print(result.stderr.decode("utf-8", errors="replace").strip()[:400])
            return False

    if not HasDjvuMagic(djvu_path):
        print(f"djvu export failed: {pdf_path.name}")
        return False
    print(f"djvu {djvu_path}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert a documentation PDF to DjVu")
    parser.add_argument("--pdf", required=True, help="source PDF")
    parser.add_argument("--djvu", required=True, help="destination DjVu")
    arguments = parser.parse_args()
    return 0 if BuildDjvu(pathlib.Path(arguments.pdf), pathlib.Path(arguments.djvu)) else 1


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    raise SystemExit(main())
