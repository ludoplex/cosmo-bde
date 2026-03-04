#!/usr/bin/env python3
"""Run a deterministic parity harness over CodeSmith corpus fixtures.

This is an execution scaffold for staged parity work:
  - dry-run mode validates fixture discovery and corpus integrity
  - engine mode executes a command per fixture and captures stdout/stderr
  - baseline mode compares captured output to golden snapshots
"""

from __future__ import annotations

import argparse
import fnmatch
import json
from pathlib import Path, PurePosixPath
import shlex
import subprocess
from typing import Dict, Iterable, List


DEFAULT_RUN_EXTS = (".cst", ".csp", ".csmap")


def read_lock(path: Path) -> Dict[str, object]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def safe_relpath(parts: Iterable[str]) -> PurePosixPath:
    path = PurePosixPath(*parts)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"unsafe path {path}")
    return path


def collect_fixtures(
    lock: Dict[str, object],
    corpus_dir: Path,
    include_exts: List[str],
    pattern: str,
    limit: int,
) -> List[Dict[str, str]]:
    entries = lock.get("entries")
    if not isinstance(entries, list):
        raise ValueError("invalid lock file: entries must be list")

    include_set = {x.lower() for x in include_exts}
    fixtures: List[Dict[str, str]] = []

    for item in entries:
        if not isinstance(item, dict):
            continue
        ext = str(item.get("ext", "")).lower()
        if ext not in include_set:
            continue

        container = str(item.get("container", ""))
        entry = str(item.get("entry", ""))
        rel_root = "outer" if container == "outer" else f"nested/{container}"
        rel = safe_relpath([rel_root, entry])
        rel_str = rel.as_posix()

        if pattern and not fnmatch.fnmatch(rel_str, pattern):
            continue

        fixture_path = corpus_dir.joinpath(*rel.parts)
        fixtures.append(
            {
                "container": container,
                "entry": entry,
                "rel": rel_str,
                "input": str(fixture_path),
            }
        )

        if limit > 0 and len(fixtures) >= limit:
            break

    return fixtures


def build_command(engine: str, input_path: str) -> str:
    quoted_input = shlex.quote(input_path)
    if "{input}" in engine:
        return engine.replace("{input}", quoted_input)
    return f"{engine} {quoted_input}"


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CodeSmith parity harness")
    parser.add_argument("--inventory", required=True, help="path to corpus lock json")
    parser.add_argument("--corpus-dir", required=True, help="path to extracted corpus")
    parser.add_argument("--artifacts-dir", required=True, help="path for run outputs")
    parser.add_argument("--engine", default="", help="engine command, optional {input} token")
    parser.add_argument("--baseline-dir", default="", help="golden baseline output directory")
    parser.add_argument("--write-baseline", action="store_true", help="write baseline from current output")
    parser.add_argument("--dry-run", action="store_true", help="validate fixture discovery only")
    parser.add_argument("--pattern", default="", help="fnmatch filter on fixture relative path")
    parser.add_argument("--limit", type=int, default=0, help="max fixtures (0 = all)")
    parser.add_argument(
        "--ext",
        nargs="+",
        default=list(DEFAULT_RUN_EXTS),
        help="extensions to run (default: %(default)s)",
    )
    args = parser.parse_args()

    inventory = Path(args.inventory).resolve()
    corpus_dir = Path(args.corpus_dir).resolve()
    artifacts_dir = Path(args.artifacts_dir).resolve()
    baseline_dir = Path(args.baseline_dir).resolve() if args.baseline_dir else None

    lock = read_lock(inventory)
    fixtures = collect_fixtures(lock, corpus_dir, args.ext, args.pattern, args.limit)
    if not fixtures:
        print("no fixtures selected")
        return 1

    missing = [f["rel"] for f in fixtures if not Path(f["input"]).is_file()]
    if missing:
        print("missing fixture files:")
        for rel in missing[:20]:
            print(f"  {rel}")
        if len(missing) > 20:
            print(f"  ... ({len(missing) - 20} more)")
        return 2

    if args.dry_run or not args.engine:
        print(f"dry-run ok: {len(fixtures)} fixtures discovered")
        return 0

    artifacts_dir.mkdir(parents=True, exist_ok=True)
    exit_failures = 0
    baseline_failures = 0

    for fixture in fixtures:
        rel = PurePosixPath(fixture["rel"])
        command = build_command(args.engine, fixture["input"])
        run = subprocess.run(command, shell=True, text=True, capture_output=True)

        out_file = artifacts_dir.joinpath(*rel.parts).with_suffix(rel.suffix + ".stdout")
        err_file = artifacts_dir.joinpath(*rel.parts).with_suffix(rel.suffix + ".stderr")
        code_file = artifacts_dir.joinpath(*rel.parts).with_suffix(rel.suffix + ".exitcode")

        write_text(out_file, run.stdout)
        write_text(err_file, run.stderr)
        write_text(code_file, f"{run.returncode}\n")

        if run.returncode != 0:
            exit_failures += 1

        if baseline_dir is not None:
            baseline_out = baseline_dir.joinpath(*rel.parts).with_suffix(rel.suffix + ".stdout")
            if args.write_baseline:
                write_text(baseline_out, run.stdout)
            elif baseline_out.exists():
                expected = baseline_out.read_text(encoding="utf-8")
                if expected != run.stdout:
                    baseline_failures += 1
            else:
                baseline_failures += 1

    print(f"fixtures run: {len(fixtures)}")
    print(f"non-zero exit codes: {exit_failures}")
    if baseline_dir is not None:
        print(f"baseline mismatches: {baseline_failures}")

    if exit_failures or baseline_failures:
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
