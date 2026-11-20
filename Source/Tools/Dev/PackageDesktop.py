#!/usr/bin/env python3
"""Builds and packages the FemboyEngine2D desktop release bundle.

The package contains the editor, sandbox, asset tools, notices and a source
offer. It is intentionally a Python entry point so Windows, Linux and macOS use
the same manifest and archive rules; the OS setup directories contain thin
wrappers around this file.

    python Source/Tools/Dev/PackageDesktop.py --preset windows-release
    python Source/Tools/Dev/PackageDesktop.py --preset linux-release --skip-build
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tarfile
import zipfile

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[3]
PRESETS = {
    "windows": ("windows-release", "build-windows-release"),
    "linux": ("linux-release", "build-linux-release"),
    "macos": ("macos-release", "build-macos-release"),
}
VERSION = (ROOT / "Version.txt").read_text(encoding="utf-8").strip()
SOURCE_URL = "https://github.com/Razraboychik/FemboyEngine-2D"


def default_platform() -> str:
    if sys.platform.startswith("win"):
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return "linux"


def digest(path: pathlib.Path) -> str:
    hash_object = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            hash_object.update(chunk)
    return hash_object.hexdigest()


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", choices=sorted(PRESETS), default=default_platform())
    parser.add_argument("--preset", default="")
    parser.add_argument("--build-dir", default="")
    parser.add_argument("--output", default="dist")
    parser.add_argument("--version", default=VERSION)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--no-archive", action="store_true")
    parser.add_argument("--max-size-mb", type=float, default=0.0,
                        help="fail when the unpacked bundle exceeds this size (0 disables the cap)")
    parser.add_argument("--game", action="store_true",
                        help="bundle the game runtime only: sandbox, server and runtime DLLs, no editor or tools")
    parser.add_argument("--features", default="all",
                        help="game vendor features: all, none, or a comma list of fsr,dlss,xess")
    parser.add_argument("--assets", default="",
                        help="optional asset directory to cook into game/assets.fempak")
    args = parser.parse_args()

    preset, build_dir = PRESETS[args.platform]
    if args.preset:
        preset = args.preset
    build_root = ROOT / (args.build_dir or build_dir)
    if not args.skip_build:
        run(["cmake", "--preset", preset])
        run(["cmake", "--build", "--preset", preset, "--parallel", "8"])

    binary_root = build_root / "bin"
    tools_root = binary_root / "tools"
    suffix = ".exe" if args.platform == "windows" else ""
    if args.game:
        artifacts = {
            "bin/fby_sandbox" + suffix: binary_root / ("fby_sandbox" + suffix),
            "bin/fby_server" + suffix: binary_root / ("fby_server" + suffix),
            "LICENSE": ROOT / "LICENSE",
            "THIRD_PARTY_NOTICES.md": ROOT / "THIRD_PARTY_NOTICES.md",
        }
        # The game only needs the runtime DLLs. Editor, tools and test binaries
        # stay out, which is the single largest packaging win.
        for source in sorted(binary_root.iterdir()):
            if source.suffix in (".dll", ".so", ".dylib") and not source.name.startswith(
                    ("amd_fidelityfx_", "nvngx_dlss", "libxess")):
                artifacts["bin/" + source.name] = source
    else:
        artifacts = {
            "bin/fby_editor" + suffix: binary_root / ("fby_editor" + suffix),
            "bin/fby_sandbox" + suffix: binary_root / ("fby_sandbox" + suffix),
            "bin/tools/fby_fempak" + suffix: tools_root / ("fby_fempak" + suffix),
            "bin/tools/fby_scene_tool" + suffix: tools_root / ("fby_scene_tool" + suffix),
            "bin/tools/fby_aotc" + suffix: tools_root / ("fby_aotc" + suffix),
            "LICENSE": ROOT / "LICENSE",
            "THIRD_PARTY_NOTICES.md": ROOT / "THIRD_PARTY_NOTICES.md",
            "README.md": ROOT / "README.md",
            "Documentation/platform-support.md": ROOT / "Documentation" / "design" / "platform-support.md",
        }
        for source in sorted(binary_root.iterdir()):
            if source.suffix in (".dll", ".so", ".dylib") and not source.name.startswith(
                    ("amd_fidelityfx_", "nvngx_dlss", "libxess")):
                artifacts["bin/" + source.name] = source

    missing = [relative for relative, source in artifacts.items() if not source.is_file()]
    if missing:
        for relative in missing:
            print(f"package: missing artifact {relative}", file=sys.stderr)
        return 1

    # Vendor runtimes are optional and live beside the executables. Copy them
    # into the bundle so a release-vendor build is runnable without the SDK tree.
    feature_names = {name.strip().lower() for name in args.features.split(",") if name.strip()}
    vendor_runtime_patterns: list[str] = []
    if not feature_names or "all" in feature_names:
        vendor_runtime_patterns = [
            "amd_fidelityfx_*.dll", "amd_fidelityfx_*.so*", "nvngx_dlss*.dll", "nvngx_dlss*.so*",
            "libxess*.dll", "libxess*.so*",
        ]
    else:
        if "fsr" in feature_names:
            vendor_runtime_patterns.extend(["amd_fidelityfx_*.dll", "amd_fidelityfx_*.so*"])
        if "dlss" in feature_names:
            vendor_runtime_patterns.extend(["nvngx_dlss*.dll", "nvngx_dlss*.so*"])
        if "xess" in feature_names:
            vendor_runtime_patterns.extend(["libxess*.dll", "libxess*.so*"])
    for pattern in vendor_runtime_patterns:
        for source in sorted(binary_root.glob(pattern)):
            relative = "bin/" + source.name
            if relative not in artifacts:
                artifacts[relative] = source

    bundle_name = f"FemboyEngine2D-{args.platform}-{args.version}"
    bundle = ROOT / args.output / bundle_name
    if bundle.exists():
        shutil.rmtree(bundle)
    bundle.mkdir(parents=True)

    manifest_entries: list[dict[str, object]] = []
    for relative, source in artifacts.items():
        destination = bundle / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        manifest_entries.append(
            {
                "path": relative,
                "bytes": destination.stat().st_size,
                "sha256": digest(destination),
            }
        )

    if args.game and args.assets:
        pak_path = bundle / "game" / "assets.fempak"
        pak_path.parent.mkdir(parents=True, exist_ok=True)
        fempak = tools_root / ("fby_fempak" + suffix)
        subprocess.run(
            [str(fempak), "cook", "--root", str(ROOT / args.assets), "--out", str(pak_path)],
            cwd=ROOT,
            check=True,
        )
        manifest_entries.append(
            {
                "path": pak_path.relative_to(bundle).as_posix(),
                "bytes": pak_path.stat().st_size,
                "sha256": digest(pak_path),
            }
        )

    source_offer = bundle / "SOURCE_OFFER.txt"
    source_offer.write_text(
        "FemboyEngine2D is MPL-2.0. The corresponding source is available at\n"
        f"{SOURCE_URL}/tree/v{args.version}\n",
        encoding="utf-8",
    )
    manifest_entries.append(
        {
            "path": "SOURCE_OFFER.txt",
            "bytes": source_offer.stat().st_size,
            "sha256": digest(source_offer),
        }
    )

    manifest = {
        "name": bundle_name,
        "version": args.version,
        "platform": args.platform,
        "preset": preset,
        "artifacts": manifest_entries,
    }
    manifest_path = bundle / "release-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    entries = sorted(
        ((path.stat().st_size, path.relative_to(bundle).as_posix()) for path in bundle.rglob("*") if path.is_file()),
        reverse=True,
    )
    total_bytes = sum(size for size, _ in entries)
    print(f"package: bundle {total_bytes / (1024 * 1024):.1f} MiB")
    for size, relative in entries[:5]:
        print(f"package:   {size / (1024 * 1024):6.2f} MiB  {relative}")
    if args.max_size_mb > 0.0 and total_bytes > args.max_size_mb * 1024 * 1024:
        print(f"package: bundle exceeds the {args.max_size_mb:.1f} MiB cap", file=sys.stderr)
        return 1

    if not args.no_archive:
        archive_dir = ROOT / args.output
        if args.platform == "windows":
            archive_path = archive_dir / f"{bundle_name}.zip"
            with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                for path in sorted(bundle.rglob("*")):
                    if path.is_file():
                        archive.write(path, path.relative_to(bundle.parent).as_posix())
        else:
            archive_path = archive_dir / f"{bundle_name}.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                archive.add(bundle, arcname=bundle_name)
        print(f"package: {archive_path} ({archive_path.stat().st_size / (1024 * 1024):.1f} MiB)")

    print(f"package: {bundle}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
