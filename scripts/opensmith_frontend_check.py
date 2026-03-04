#!/usr/bin/env python3
"""Run OpenSmith parser front-end checks over extracted corpus fixtures."""

from __future__ import annotations

import argparse
import fnmatch
import json
from pathlib import Path, PurePosixPath
import subprocess
from typing import Dict, Iterable, List


DEFAULT_EXTS = (".cst", ".csp", ".csmap")


def read_lock(path: Path) -> Dict[str, object]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def safe_relpath(parts: Iterable[str]) -> PurePosixPath:
    p = PurePosixPath(*parts)
    if p.is_absolute() or ".." in p.parts:
        raise ValueError(f"unsafe relative path: {p}")
    return p


def collect_fixtures(
    lock: Dict[str, object],
    corpus_dir: Path,
    include_exts: List[str],
    pattern: str,
    limit: int,
) -> List[Path]:
    entries = lock.get("entries")
    if not isinstance(entries, list):
        raise ValueError("lock file has no valid entries list")

    ext_set = {ext.lower() for ext in include_exts}
    out: List[Path] = []

    for entry in entries:
        if not isinstance(entry, dict):
            continue

        ext = str(entry.get("ext", "")).lower()
        if ext not in ext_set:
            continue

        container = str(entry.get("container", ""))
        member = str(entry.get("entry", ""))

        rel_root = "outer" if container == "outer" else f"nested/{container}"
        rel = safe_relpath([rel_root, member])
        rel_str = rel.as_posix()

        if pattern and not fnmatch.fnmatch(rel_str, pattern):
            continue

        out.append(corpus_dir.joinpath(*rel.parts))
        if limit > 0 and len(out) >= limit:
            break

    return out


def run_command(cmd: List[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="OpenSmith front-end fixture checker")
    parser.add_argument("--tool", required=True, help="path to opensmithgen tool")
    parser.add_argument("--inventory", required=True, help="corpus lock json path")
    parser.add_argument("--corpus-dir", required=True, help="extracted corpus directory")
    parser.add_argument("--pattern", default="", help="optional fnmatch filter")
    parser.add_argument("--limit", type=int, default=0, help="fixture limit (0 = all)")
    parser.add_argument(
        "--ext",
        nargs="+",
        default=list(DEFAULT_EXTS),
        help="extensions to run (default: %(default)s)",
    )
    args = parser.parse_args()

    tool = Path(args.tool).resolve()
    inventory = Path(args.inventory).resolve()
    corpus_dir = Path(args.corpus_dir).resolve()

    if not tool.is_file():
        print(f"tool missing: {tool}")
        return 2
    if not inventory.is_file():
        print(f"inventory missing: {inventory}")
        return 2

    lock = read_lock(inventory)
    fixtures = collect_fixtures(lock, corpus_dir, args.ext, args.pattern, args.limit)
    if not fixtures:
        print("no fixtures selected")
        return 1

    missing = [p for p in fixtures if not p.is_file()]
    if missing:
        print("missing fixture files:")
        for p in missing[:20]:
            print(f"  {p}")
        if len(missing) > 20:
            print(f"  ... ({len(missing) - 20} more)")
        return 2

    roundtrip_fail = 0
    ast_fail = 0

    for fixture in fixtures:
        roundtrip = run_command([str(tool), "--check-roundtrip", str(fixture)])
        if roundtrip.returncode != 0:
            roundtrip_fail += 1
            if roundtrip_fail <= 20:
                print(f"roundtrip FAIL: {fixture}")
                if roundtrip.stderr.strip():
                    print(roundtrip.stderr.strip())
            continue

        ast = run_command([str(tool), "--ast", str(fixture)])
        if ast.returncode != 0:
            ast_fail += 1
            if ast_fail <= 20:
                print(f"ast FAIL: {fixture}")
                if ast.stderr.strip():
                    print(ast.stderr.strip())
            continue

        try:
            payload = json.loads(ast.stdout)
            if not isinstance(payload, dict) or "nodes" not in payload:
                raise ValueError("invalid ast payload")
        except Exception as ex:  # noqa: BLE001
            ast_fail += 1
            if ast_fail <= 20:
                print(f"ast json FAIL: {fixture}")
                print(str(ex))

    print(f"fixtures checked: {len(fixtures)}")
    print(f"roundtrip failures: {roundtrip_fail}")
    print(f"ast failures: {ast_fail}")

    if roundtrip_fail or ast_fail:
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
