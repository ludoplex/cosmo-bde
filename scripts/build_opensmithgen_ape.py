#!/usr/bin/env python3
"""Build opensmithgen as an APE binary with Cosmopolitan prebuilts."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
from typing import List


def detect_cosmocc_root(explicit: str) -> Path:
    candidates: List[Path] = []

    if explicit:
        candidates.append(Path(explicit))

    env_root = os.environ.get("COSMOCC_ROOT", "")
    if env_root:
        candidates.append(Path(env_root))

    home = Path.home()
    candidates.append(home / ".cosmocc")
    username = os.environ.get("USERNAME", "")
    if username:
        candidates.append(Path(f"C:/Users/{username}/.cosmocc"))
        candidates.append(Path(f"/Users/{username}/.cosmocc"))

    for root in candidates:
        if (root / "bin" / "x86_64-linux-cosmo-gcc").is_file():
            return root.resolve()

    raise FileNotFoundError(
        "could not find Cosmopolitan prebuilts; expected x86_64-linux-cosmo-gcc in <root>/bin"
    )


def run_cmd(cmd: List[str], verbose: bool) -> None:
    if verbose:
        print("$", " ".join(cmd))
    proc = subprocess.run(cmd, text=True, capture_output=True)
    if proc.returncode != 0:
        if proc.stdout.strip():
            print(proc.stdout)
        if proc.stderr.strip():
            print(proc.stderr)
        raise RuntimeError(f"command failed with rc={proc.returncode}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build opensmithgen APE binary")
    parser.add_argument(
        "--source",
        default="tools/opensmithgen/opensmithgen.c",
        help="path to opensmithgen source file",
    )
    parser.add_argument(
        "--output",
        default="build/opensmithgen.com",
        help="output APE binary path",
    )
    parser.add_argument(
        "--cosmocc-root",
        default="",
        help="path to Cosmopolitan prebuilt root (defaults to ~/.cosmocc)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="print invoked commands",
    )
    args = parser.parse_args()

    source = Path(args.source).resolve()
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    if not source.is_file():
        print(f"source missing: {source}")
        return 2

    try:
        root = detect_cosmocc_root(args.cosmocc_root)
    except FileNotFoundError as ex:
        print(str(ex))
        return 2

    bindir = root / "bin"
    libdir = root / "x86_64-linux-cosmo" / "lib"
    includedir = root / "include"

    gcc = bindir / "x86_64-linux-cosmo-gcc"
    fixupobj = bindir / "fixupobj"
    objcopy = bindir / "x86_64-linux-cosmo-objcopy"
    zipcopy = bindir / "zipcopy"

    required = [gcc, fixupobj, objcopy, zipcopy, libdir / "ape.lds", libdir / "crt.o", libdir / "ape-no-modify-self.o"]
    missing = [str(p) for p in required if not p.exists()]
    if missing:
        print("cosmocc prebuilt is incomplete; missing paths:")
        for p in missing:
            print(f"  {p}")
        return 2

    dbg = output.with_name(output.name + ".dbg")

    compile_cmd = [
        str(gcc),
        "-D__COSMOPOLITAN__",
        "-D__COSMOCC__",
        "-D_COSMO_SOURCE",
        "-include",
        "libc/integral/normalize.inc",
        "-fportcosmo",
        "-fno-semantic-interposition",
        "-Wno-implicit-int",
        "-mno-tls-direct-seg-refs",
        "-fno-pie",
        "-nostdinc",
        "-isystem",
        str(includedir),
        "-mno-red-zone",
        "-O2",
        "-Wall",
        "-Werror",
        "-std=c11",
        "-Wno-stringop-truncation",
        "-fno-omit-frame-pointer",
        "-fno-schedule-insns2",
        "-I",
        str(source.parent),
        str(libdir / "ape-no-modify-self.o"),
        str(libdir / "crt.o"),
        str(source),
        "-static",
        "-no-pie",
        "-nostdlib",
        "-fuse-ld=bfd",
        "-Wl,-z,noexecstack",
        "-L",
        str(libdir),
        f"-Wl,-T,{libdir / 'ape.lds'}",
        "-Wl,-z,common-page-size=4096",
        "-Wl,-z,max-page-size=16384",
        "-lcosmo",
        "-o",
        str(dbg),
    ]

    try:
        run_cmd(compile_cmd, args.verbose)
        run_cmd([str(fixupobj), str(dbg)], args.verbose)
        run_cmd([str(objcopy), "-S", "-O", "binary", str(dbg), str(output)], args.verbose)
        run_cmd([str(zipcopy), str(dbg), str(output)], args.verbose)
    except RuntimeError as ex:
        print(str(ex))
        return 1

    print(f"built: {output}")
    print(f"debug: {dbg}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
