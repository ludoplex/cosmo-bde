#!/usr/bin/env python3
"""Build and validate a deterministic CodeSmith fixture corpus.

This script inventories and extracts text fixtures from a CodeSmith-style
distribution zip. It supports nested sample archives, e.g.:

  Generator-85.zip
    └── Samples/Samples.zip
    └── Samples/Maps.zip

The lock file format is deterministic and intended for parity test reproducibility.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import sys
from pathlib import Path, PurePosixPath
from typing import Dict, Iterable, Iterator, List, Tuple
import zipfile


DEFAULT_EXTS = (".cst", ".csp", ".csmap", ".xsd", ".xml", ".json")


def resolve_existing_path(raw: str) -> Path:
    path = Path(raw)
    if path.exists():
        return path.resolve()

    if os.name == "nt":
        norm = raw.replace("\\", "/")
        win_candidates = []
        if norm.startswith("/c/"):
            win_candidates.append(Path("C:/" + norm[3:]))
        elif norm.startswith("/"):
            win_candidates.append(Path("C:" + norm))

        for candidate in win_candidates:
            if candidate.exists():
                return candidate.resolve()

    return path.resolve()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def normalize_posix(path: str) -> str:
    return str(PurePosixPath(path))


def has_allowed_ext(path: str, allowed_exts: Tuple[str, ...]) -> bool:
    return PurePosixPath(path).suffix.lower() in allowed_exts


def is_safe_member(path: str) -> bool:
    p = PurePosixPath(path)
    if p.is_absolute():
        return False
    if any(part == ".." for part in p.parts):
        return False
    return True


def iter_inventory(
    zip_path: Path,
    allowed_exts: Tuple[str, ...],
    nested_zip_prefix: str,
) -> Iterator[Dict[str, object]]:
    nested_zip_prefix = nested_zip_prefix.rstrip("/") + "/"
    with zipfile.ZipFile(zip_path, "r") as outer:
        infos = sorted(outer.infolist(), key=lambda i: i.filename)
        for info in infos:
            if info.is_dir():
                continue
            outer_name = normalize_posix(info.filename)
            lower_name = outer_name.lower()

            if has_allowed_ext(outer_name, allowed_exts):
                data = outer.read(info)
                yield {
                    "container": "outer",
                    "entry": outer_name,
                    "ext": PurePosixPath(outer_name).suffix.lower(),
                    "size": len(data),
                    "sha256": sha256_bytes(data),
                }

            if lower_name.startswith(nested_zip_prefix.lower()) and lower_name.endswith(".zip"):
                nested_bytes = outer.read(info)
                with zipfile.ZipFile(io.BytesIO(nested_bytes), "r") as inner:
                    inner_infos = sorted(inner.infolist(), key=lambda i: i.filename)
                    for inner_info in inner_infos:
                        if inner_info.is_dir():
                            continue
                        inner_name = normalize_posix(inner_info.filename)
                        if not has_allowed_ext(inner_name, allowed_exts):
                            continue
                        data = inner.read(inner_info)
                        yield {
                            "container": outer_name,
                            "entry": inner_name,
                            "ext": PurePosixPath(inner_name).suffix.lower(),
                            "size": len(data),
                            "sha256": sha256_bytes(data),
                        }


def build_lock(
    zip_path: Path,
    allowed_exts: Tuple[str, ...],
    nested_zip_prefix: str,
) -> Dict[str, object]:
    entries = list(iter_inventory(zip_path, allowed_exts, nested_zip_prefix))
    entries.sort(key=lambda e: (str(e["container"]), str(e["entry"])))
    by_ext: Dict[str, int] = {}
    for entry in entries:
        ext = str(entry["ext"])
        by_ext[ext] = by_ext.get(ext, 0) + 1
    return {
        "schema": 1,
        "source_zip": zip_path.name,
        "source_zip_sha256": sha256_file(zip_path),
        "nested_zip_prefix": nested_zip_prefix,
        "extensions": list(allowed_exts),
        "total_entries": len(entries),
        "counts_by_ext": by_ext,
        "entries": entries,
    }


def write_json(path: Path, payload: Dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(payload, f, sort_keys=True, indent=2)
        f.write("\n")


def read_json(path: Path) -> Dict[str, object]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def extract_corpus(zip_path: Path, lock: Dict[str, object], out_dir: Path) -> None:
    entries = lock.get("entries")
    if not isinstance(entries, list):
        raise ValueError("lock file missing entries list")

    source_sha = lock.get("source_zip_sha256")
    if isinstance(source_sha, str):
        current_sha = sha256_file(zip_path)
        if current_sha != source_sha:
            raise ValueError(
                f"zip sha256 mismatch: expected {source_sha}, got {current_sha}"
            )

    out_dir.mkdir(parents=True, exist_ok=True)
    nested_cache: Dict[str, zipfile.ZipFile] = {}

    with zipfile.ZipFile(zip_path, "r") as outer:
        try:
            for entry in entries:
                if not isinstance(entry, dict):
                    continue
                container = str(entry.get("container", ""))
                member = str(entry.get("entry", ""))
                expected_sha = str(entry.get("sha256", ""))

                if not is_safe_member(member):
                    raise ValueError(f"unsafe member path in lock file: {member}")
                if container != "outer" and not is_safe_member(container):
                    raise ValueError(f"unsafe container path in lock file: {container}")

                if container == "outer":
                    data = outer.read(member)
                else:
                    zf = nested_cache.get(container)
                    if zf is None:
                        nested_bytes = outer.read(container)
                        zf = zipfile.ZipFile(io.BytesIO(nested_bytes), "r")
                        nested_cache[container] = zf
                    data = zf.read(member)

                digest = sha256_bytes(data)
                if expected_sha and digest != expected_sha:
                    raise ValueError(
                        f"sha256 mismatch for {container}!{member}: expected {expected_sha}, got {digest}"
                    )

                rel_root = "outer" if container == "outer" else f"nested/{container}"
                rel_path = PurePosixPath(rel_root) / PurePosixPath(member)
                dst = out_dir.joinpath(*rel_path.parts)
                dst.parent.mkdir(parents=True, exist_ok=True)
                with dst.open("wb") as f:
                    f.write(data)
        finally:
            for zf in nested_cache.values():
                zf.close()


def cmd_inventory(args: argparse.Namespace) -> int:
    zip_path = resolve_existing_path(args.zip)
    lock_path = Path(args.lock).resolve()
    allowed_exts = tuple(ext.lower() for ext in args.ext)
    lock = build_lock(zip_path, allowed_exts, args.nested_zip_prefix)
    write_json(lock_path, lock)
    print(f"wrote lock: {lock_path}")
    print(f"entries: {lock['total_entries']}")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    zip_path = resolve_existing_path(args.zip)
    lock_path = Path(args.lock).resolve()
    existing = read_json(lock_path)
    ext_value = existing.get("extensions", DEFAULT_EXTS)
    if isinstance(ext_value, list):
        allowed_exts = tuple(str(ext).lower() for ext in ext_value)
    else:
        allowed_exts = DEFAULT_EXTS
    nested_zip_prefix = str(existing.get("nested_zip_prefix", args.nested_zip_prefix))
    current = build_lock(zip_path, allowed_exts, nested_zip_prefix)
    if existing != current:
        print("lock mismatch: regenerate with inventory command", file=sys.stderr)
        return 2
    print(f"lock verified: {lock_path}")
    print(f"entries: {current['total_entries']}")
    return 0


def cmd_extract(args: argparse.Namespace) -> int:
    zip_path = resolve_existing_path(args.zip)
    lock_path = Path(args.lock).resolve()
    out_dir = Path(args.out_dir).resolve()
    lock = read_json(lock_path)
    extract_corpus(zip_path, lock, out_dir)
    print(f"extracted corpus: {out_dir}")
    print(f"entries: {lock.get('total_entries', 0)}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="CodeSmith corpus inventory/extract tool")
    sub = parser.add_subparsers(dest="cmd", required=True)

    inv = sub.add_parser("inventory", help="build lock file from zip")
    inv.add_argument("--zip", required=True, help="path to Generator-85.zip")
    inv.add_argument("--lock", required=True, help="output lock json")
    inv.add_argument(
        "--ext",
        nargs="+",
        default=list(DEFAULT_EXTS),
        help="extensions to include (default: %(default)s)",
    )
    inv.add_argument(
        "--nested-zip-prefix",
        default="Samples",
        help="scan nested zips under this outer zip prefix",
    )
    inv.set_defaults(func=cmd_inventory)

    ver = sub.add_parser("verify", help="verify lock file matches zip")
    ver.add_argument("--zip", required=True, help="path to Generator-85.zip")
    ver.add_argument("--lock", required=True, help="existing lock json")
    ver.add_argument(
        "--nested-zip-prefix",
        default="Samples",
        help="fallback nested zip prefix if lock does not define one",
    )
    ver.set_defaults(func=cmd_verify)

    ext = sub.add_parser("extract", help="extract lock-defined corpus")
    ext.add_argument("--zip", required=True, help="path to Generator-85.zip")
    ext.add_argument("--lock", required=True, help="lock json to extract")
    ext.add_argument("--out-dir", required=True, help="destination directory")
    ext.set_defaults(func=cmd_extract)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
