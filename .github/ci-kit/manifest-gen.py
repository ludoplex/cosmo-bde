#!/usr/bin/env python3
"""manifest-kit: canonical FUNCTION_MANIFEST / FUNCTION_SUBMANIFEST generator.

Consolidates the 7+ hand-forked per-repo registry generators (see README.md)
into ONE config-driven, Python-3-stdlib-only tool. Emits:

  * FUNCTION_MANIFEST.md      - repo-level index of declaration-bearing dirs
  * FUNCTION_SUBMANIFEST.md   - one per directory with >=1 indexed declaration
  * optional Python registry  - docs/function-registry.md style (ast: signatures,
                                classes+methods, decorators, module vars)
  * optional file inventory   - MANIFEST.json + submanifest_<component>.json
                                (path/component/lang/bytes/sha256/lines)

Languages: Python (stdlib ast - exact; or regex for mhi-procurement parity),
JS/TS, POSIX/bash shell, SQL DDL, C/C++ (multi-line signature tracking), Go,
Rust. Scanner strategy per language is the best-of-breed taken from the
original forks; compat knobs keep migration diffs near zero (see MIGRATION.md).

Usage:
    python3 manifest-gen.py [ROOT] [--config FILE] [--output-dir DIR]
                            [--print-config] [--version]

Config: ROOT/manifest.config.json (every key optional; see DEFAULT_CONFIG).
--output-dir mirrors all writes into DIR instead of the repo (read-only
verification). Deterministic: no timestamps, no randomness, no network.
Exit codes: 0 ok - 1 I/O failure - 2 usage/config error.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import re
import sys
from typing import Callable

KIT_VERSION = "1.0.0"

# Declaration record: (line, name, kind, context)
Decl = tuple

DEFAULT_CONFIG: dict = {
    # --- outputs ---
    "root_manifest": "FUNCTION_MANIFEST.md",
    "submanifest_name": "FUNCTION_SUBMANIFEST.md",
    "py_registry": None,        # e.g. "docs/function-registry.md"
    "inventory": None,          # e.g. {"json_dir": ".manifests-json", ...}
    # --- walking ---
    "ignored_dirs": [
        ".git", ".github", ".vscode", "__pycache__", "node_modules",
        "venv", ".venv", "dist", "build", "target", "coverage",
        ".mypy_cache", ".pytest_cache", ".ruff_cache",
    ],
    "ignored_paths": [],        # repo-relative prefixes, e.g. "desktop/angular/dist"
    "generated_basenames": [],  # regexes of basenames to skip as generated
    "skip_hidden_dirs": False,
    "hidden_dir_allowlist": [".claude"],
    "encoding_errors": "ignore",  # "ignore" (index anyway) | "skip" (drop non-UTF8 file)
    # --- scanning ---
    "languages": ["python", "javascript", "shell", "sql"],
    "python_scanner": "ast",    # "ast" (module-level + methods) | "regex" (every def)
    "c_scanner": "functions",   # "functions" (defs/protos) | "full" (+ types, #define)
    "sql_scanner": "full",      # "full" (all DDL + INSERT INTO) | "schema_only"
    # --- rendering ---
    "generator_label": "manifest-gen.py (manifest-kit)",
    "description": ("Deterministic index of source declarations plus "
                    "per-directory submanifests."),
    "include_context": False,   # append "; context: ..." to each declaration line
    "context_overrides": {},    # kind -> context label override, e.g. {"shell_function": "source"}
    "named_wording": False,     # "named declarations" wording (mhi-procurement)
    "coverage_rules": None,     # list of bullet lines for a "## Coverage Rules" section
    "directory_notes": [],      # bullet lines for submanifest "## Directory Notes"
    "prune_stale": True,
}

LANG_EXTENSIONS: dict[str, list[str]] = {
    "python": [".py"],
    "javascript": [".js", ".jsx", ".mjs", ".cjs", ".ts", ".tsx"],
    "shell": [".sh", ".bash"],
    "sql": [".sql"],
    "c": [".c", ".h", ".cc", ".cpp", ".cxx", ".hpp"],
    "go": [".go"],
    "rust": [".rs"],
    "kotlin": [".kt", ".kts"],
    "swift": [".swift"],
    "powershell": [".ps1", ".psm1"],
}


def die(msg: str, code: int = 1) -> None:
    print(f"manifest-gen: ERROR: {msg}", file=sys.stderr)
    raise SystemExit(code)


# ---------------------------------------------------------------- scanners --

def scan_python_ast(src: str) -> list[Decl]:
    """Exact module-level functions/classes + one level of methods (stdlib ast).
    A syntax error is itself a recorded finding, not a crash."""
    try:
        tree = ast.parse(src)
    except SyntaxError as exc:
        return [(exc.lineno or 1, f"<SYNTAX ERROR: {exc.msg}>", "py_parse_error", "source")]
    out: list[Decl] = []
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            out.append((node.lineno, node.name, "py_func", "top-level"))
        elif isinstance(node, ast.ClassDef):
            out.append((node.lineno, node.name, "py_class", "top-level"))
            for sub in node.body:
                if isinstance(sub, (ast.FunctionDef, ast.AsyncFunctionDef)):
                    out.append((sub.lineno, f"{node.name}.{sub.name}", "py_method", "class"))
    return out


_PY_DEF = re.compile(r"^(\s*)(async\s+def|def)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def scan_python_regex(src: str) -> list[Decl]:
    """Every def/async def at any nesting depth (mhi-procurement parity)."""
    out: list[Decl] = []
    for i, line in enumerate(src.splitlines(), 1):
        m = _PY_DEF.match(line)
        if m:
            kind = "python_async_def" if m.group(2).startswith("async") else "python_def"
            out.append((i, m.group(3), kind, "top-level" if not m.group(1) else "nested"))
    return out


_JS_PATTERNS = [
    ("js_function", re.compile(
        r"^\s*(?:export\s+)?(?:async\s+)?function\s+([A-Za-z_$][\w$]*)\s*\(")),
    ("js_function_expr", re.compile(
        r"^\s*(?:export\s+)?(?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*=\s*(?:async\s+)?function\b")),
    ("js_arrow_binding", re.compile(
        r"^\s*(?:export\s+)?(?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*=\s*"
        r"(?:async\s*)?(?:\([^)]*\)|[A-Za-z_$][\w$]*)\s*=>")),
    ("js_class", re.compile(r"^\s*(?:export\s+)?(?:abstract\s+)?class\s+([A-Za-z_$][\w$]*)\b")),
]


def scan_javascript(src: str) -> list[Decl]:
    out: list[Decl] = []
    for i, line in enumerate(src.splitlines(), 1):
        for kind, pat in _JS_PATTERNS:
            m = pat.match(line)
            if m:
                out.append((i, m.group(1), kind, "source"))
                break
    return out


_SH_PATTERNS = [
    re.compile(r"^\s*(?:function\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\(\)\s*\{"),
    re.compile(r"^\s*function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{"),
]


_PS_FUNC = re.compile(r"^\s*(?:function|filter|workflow)\s+(?:global:|script:|local:|private:)?([A-Za-z_][\w-]*)", re.I)
_PS_PARAM = re.compile(r"^\s*param\s*\(", re.I)
_PS_CLASS = re.compile(r"^\s*(?:class|enum)\s+([A-Za-z_]\w*)", re.I)


def scan_powershell(src: str) -> list[Decl]:
    """PowerShell: `function Verb-Noun {` (hyphenated names, scope prefixes), classes/enums,
    and the script-level `param(` block (reported once as `param`) so a task-invoked
    script's entry contract is visible in the manifest."""
    out: list[Decl] = []
    depth = 0
    seen_param = False
    for i, line in enumerate(src.splitlines(), 1):
        m = _PS_FUNC.match(line)
        if m:
            out.append((i, m.group(1), "ps_function", "top-level" if depth == 0 else "nested"))
        elif (m := _PS_CLASS.match(line)):
            out.append((i, m.group(1), "ps_class", "top-level"))
        elif not seen_param and depth == 0 and _PS_PARAM.match(line):
            out.append((i, "param", "ps_param_block", "script"))
            seen_param = True
        depth += line.count("{") - line.count("}")
    return out


def scan_shell(src: str) -> list[Decl]:
    out: list[Decl] = []
    for i, line in enumerate(src.splitlines(), 1):
        for pat in _SH_PATTERNS:
            m = pat.match(line)
            if m:
                out.append((i, m.group(1), "shell_function", "top-level"))
                break
    return out


_SQL_NAME = r'"?([A-Za-z_][\w.$]*)"?'
_SQL_PATTERNS = [
    ("sql_function", re.compile(
        r"^\s*CREATE\s+(?:OR\s+REPLACE\s+)?FUNCTION\s+" + _SQL_NAME, re.I)),
    ("sql_table", re.compile(
        r"^\s*CREATE\s+TABLE\s+(?:IF\s+NOT\s+EXISTS\s+)?" + _SQL_NAME, re.I)),
    ("sql_view", re.compile(
        r"^\s*CREATE\s+(?:OR\s+REPLACE\s+)?(?:MATERIALIZED\s+)?VIEW\s+"
        r"(?:IF\s+NOT\s+EXISTS\s+)?" + _SQL_NAME, re.I)),
    ("sql_index", re.compile(
        r"^\s*CREATE\s+(?:UNIQUE\s+)?INDEX\s+(?:IF\s+NOT\s+EXISTS\s+)?" + _SQL_NAME, re.I)),
    ("sql_type", re.compile(r"^\s*CREATE\s+TYPE\s+" + _SQL_NAME, re.I)),
    ("sql_extension", re.compile(
        r"^\s*CREATE\s+EXTENSION\s+(?:IF\s+NOT\s+EXISTS\s+)?" + _SQL_NAME, re.I)),
    ("sql_schema", re.compile(
        r"^\s*CREATE\s+SCHEMA\s+(?:IF\s+NOT\s+EXISTS\s+)?" + _SQL_NAME, re.I)),
    ("sql_insert_into", re.compile(r"^\s*INSERT\s+INTO\s+" + _SQL_NAME, re.I)),
]
_SQL_SCHEMA_ONLY = _SQL_PATTERNS[:4]


def scan_sql(src: str, patterns: list) -> list[Decl]:
    out: list[Decl] = []
    for i, line in enumerate(src.splitlines(), 1):
        for kind, pat in patterns:
            m = pat.match(line)
            if m:
                out.append((i, m.group(1).strip('"'), kind, "database"))
                break
    return out


_GO_DECL = re.compile(
    r"^(?:func\s+(?:\([^)]*\)\s*)?([A-Za-z_][A-Za-z0-9_]*)\s*\("
    r"|type\s+([A-Za-z_][A-Za-z0-9_]*)\s+(struct|interface)\b)")


def scan_go(src: str) -> list[Decl]:
    out: list[Decl] = []
    for i, line in enumerate(src.splitlines(), 1):
        m = _GO_DECL.match(line)
        if m:
            name = m.group(1) or m.group(2)
            kind = "go_func" if m.group(1) else f"go_{m.group(3)}"
            out.append((i, name, kind, "source"))
    return out


_RUST_DECL = re.compile(
    r"^\s*(?:pub(?:\([^)]*\))?\s+)?(?:async\s+)?(fn|struct|enum|trait|impl)\s+"
    r"([A-Za-z_][A-Za-z0-9_]*)?")


def scan_rust(src: str) -> list[Decl]:
    out: list[Decl] = []
    for i, line in enumerate(src.splitlines(), 1):
        m = _RUST_DECL.match(line)
        if m:
            out.append((i, m.group(2) or "impl", f"rust_{m.group(1)}", "source"))
    return out


_C_CONTROL = {"if", "for", "while", "switch", "return", "sizeof"}
_C_TYPE = re.compile(r"^\s*(typedef\s+)?(?:struct|enum|union)\s+([A-Za-z_][A-Za-z0-9_]*)\b")
_C_DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\b")


def _strip_c_comments(src: str) -> str:
    src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
    return re.sub(r"//.*", "", src)


def _c_candidate_name(signature: str) -> str | None:
    compact = " ".join(signature.strip().split())
    if not compact or compact.startswith("#") or compact.startswith("typedef "):
        return None
    m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*(?:\{|;)$", compact)
    if not m or m.group(1) in _C_CONTROL:
        return None
    return m.group(1)


def scan_c(src: str, mode: str) -> list[Decl]:
    """Multi-line C/C++ function definition/prototype tracking (mhi-procurement
    approach); mode "full" additionally records struct/enum/union/typedef names
    and #define macros on single lines (portmaster-.mjs approach)."""
    out: list[Decl] = []
    pending: list[tuple[int, str]] = []
    for i, line in enumerate(_strip_c_comments(src).splitlines(), 1):
        stripped = line.strip()
        if not stripped:
            pending.clear()
            continue
        if mode == "full":
            m = _C_DEFINE.match(line)
            if m:
                out.append((i, m.group(1), "c_define", "preprocessor"))
            m = _C_TYPE.match(line)
            if m:
                out.append((i, m.group(2), "c_typedef_type" if m.group(1) else "c_type", "source"))
        if not pending and len(line) - len(line.lstrip()) > 0:
            continue
        if stripped.startswith("#"):
            pending.clear()
            continue
        if "=" in stripped and not pending:
            continue
        pending.append((i, stripped))
        joined = " ".join(part for _, part in pending)
        if not (joined.endswith("{") or joined.endswith(";")):
            if len(pending) > 8:
                pending.clear()
            continue
        name = _c_candidate_name(joined)
        if name:
            kind = "c_function_definition" if joined.endswith("{") else "c_function_declaration"
            out.append((pending[0][0], name, kind, "top-level"))
        pending.clear()
    return out


_KT_MODS = r"(?:(?:public|private|internal|protected|override|open|abstract|final|suspend|inline|operator|infix|tailrec|external|actual|expect|sealed|data|enum|annotation|inner|value|const|lateinit|@\w+)\s+)*"
_KT_PATTERNS = [
    ("kotlin_fun", re.compile(
        r"^\s*" + _KT_MODS + r"fun\s+(?:<[^>]*>\s+)?(?:[\w.<>?]+\.)?"
        r"([A-Za-z_][A-Za-z0-9_]*)\s*\(")),
    ("kotlin_type", re.compile(
        r"^\s*" + _KT_MODS + r"(?:class|interface|object)\s+([A-Za-z_][A-Za-z0-9_]*)\b")),
    # top-level properties only (column 0) — canonical variables, not locals
    ("kotlin_property", re.compile(
        r"^" + _KT_MODS + r"(?:val|var)\s+([A-Za-z_][A-Za-z0-9_]*)\b")),
]


def scan_kotlin(src: str) -> list[Decl]:
    out: list[Decl] = []
    for i, line in enumerate(src.splitlines(), 1):
        for kind, pat in _KT_PATTERNS:
            m = pat.match(line)
            if m:
                out.append((i, m.group(1), kind, "source"))
                break
    return out


_SWIFT_MODS = r"(?:(?:public|private|internal|fileprivate|open|static|class|final|override|mutating|nonmutating|indirect|convenience|required|dynamic|@\w+(?:\([^)]*\))?)\s+)*"
_SWIFT_PATTERNS = [
    ("swift_func", re.compile(
        r"^\s*" + _SWIFT_MODS + r"func\s+([A-Za-z_][A-Za-z0-9_]*)\s*[(<]")),
    ("swift_type", re.compile(
        r"^\s*" + _SWIFT_MODS +
        r"(?:class|struct|enum|protocol|actor|extension)\s+([A-Za-z_][A-Za-z0-9_.]*)\b")),
    # top-level bindings only (column 0) — canonical variables, not locals
    ("swift_binding", re.compile(
        r"^" + _SWIFT_MODS + r"(?:let|var)\s+([A-Za-z_][A-Za-z0-9_]*)\b")),
]


def scan_swift(src: str) -> list[Decl]:
    out: list[Decl] = []
    for i, line in enumerate(src.splitlines(), 1):
        for kind, pat in _SWIFT_PATTERNS:
            m = pat.match(line)
            if m:
                out.append((i, m.group(1), kind, "source"))
                break
    return out


def make_scanner_map(cfg: dict) -> dict[str, Callable[[str], list]]:
    """Extension -> scanner callable, honoring the config's compat knobs."""
    sql_patterns = _SQL_PATTERNS if cfg["sql_scanner"] == "full" else _SQL_SCHEMA_ONLY
    by_lang: dict[str, Callable[[str], list]] = {
        "python": scan_python_ast if cfg["python_scanner"] == "ast" else scan_python_regex,
        "javascript": scan_javascript,
        "shell": scan_shell,
        "sql": lambda s: scan_sql(s, sql_patterns),
        "c": lambda s: scan_c(s, cfg["c_scanner"]),
        "go": scan_go,
        "rust": scan_rust,
        "kotlin": scan_kotlin,
        "swift": scan_swift,
        "powershell": scan_powershell,
    }
    ext_map: dict[str, Callable[[str], list]] = {}
    for lang in cfg["languages"]:
        if lang not in by_lang:
            die(f"unknown language in config: {lang!r} (known: {sorted(by_lang)})", 2)
        for ext in LANG_EXTENSIONS[lang]:
            ext_map[ext] = by_lang[lang]
    return ext_map


# ------------------------------------------------------------------ config --

def load_config(root: str, explicit: str | None) -> dict:
    cfg = json.loads(json.dumps(DEFAULT_CONFIG))  # deep copy
    path = explicit or os.path.join(root, "manifest.config.json")
    if explicit is None and not os.path.exists(path):
        return cfg
    try:
        with open(path, encoding="utf-8-sig") as fh:   # tolerate a BOM'd config too
            user = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        die(f"cannot load config {path}: {exc}", 2)
    if not isinstance(user, dict):
        die(f"config {path} must be a JSON object", 2)
    for key, value in user.items():
        if key not in DEFAULT_CONFIG:
            print(f"manifest-gen: WARN: unknown config key ignored: {key}", file=sys.stderr)
            continue
        cfg[key] = value
    _validate_config(cfg)
    return cfg


def _validate_config(cfg: dict) -> None:
    choices = {
        "python_scanner": {"ast", "regex"},
        "c_scanner": {"functions", "full"},
        "sql_scanner": {"full", "schema_only"},
        "encoding_errors": {"ignore", "skip"},
    }
    for key, allowed in choices.items():
        if cfg[key] not in allowed:
            die(f"config {key} must be one of {sorted(allowed)}, got {cfg[key]!r}", 2)
    for key in ("ignored_dirs", "ignored_paths", "languages", "directory_notes",
                "generated_basenames", "hidden_dir_allowlist"):
        if not isinstance(cfg[key], list):
            die(f"config {key} must be a list", 2)
    if cfg["inventory"] is not None and not isinstance(cfg["inventory"], dict):
        die("config inventory must be an object or null", 2)
    if not isinstance(cfg["context_overrides"], dict):
        die("config context_overrides must be an object", 2)


# ----------------------------------------------------------------- walking --

def _rel(path: str, root: str) -> str:
    r = os.path.relpath(path, root)
    return "." if r == "." else r.replace(os.sep, "/")


def _keep_dir(name: str, cfg: dict) -> bool:
    if name in set(cfg["ignored_dirs"]) or name.startswith("._"):
        return False
    if (cfg["skip_hidden_dirs"] and name.startswith(".")
            and name not in cfg["hidden_dir_allowlist"]):
        return False
    return True


def _ignored_path(rel: str, cfg: dict) -> bool:
    return any(rel == p.rstrip("/") or rel.startswith(p.rstrip("/") + "/")
               for p in cfg["ignored_paths"])


def _filter_dirs(dirnames: list, dirpath: str, root: str, cfg: dict) -> list:
    return sorted(
        d for d in dirnames
        if _keep_dir(d, cfg) and not _ignored_path(_rel(os.path.join(dirpath, d), root), cfg))


def _read_source(path: str, cfg: dict) -> str | None:
    strict = cfg["encoding_errors"] == "skip"
    try:
        # utf-8-sig, not utf-8: a leading BOM survives a plain utf-8 read and makes ast.parse raise
        # "invalid non-printable character U+FEFF", so a BOM'd source file silently collapses to a
        # single py_parse_error and every declaration it holds vanishes from the registry. The file
        # still RUNS fine (CPython's tokenizer strips the BOM), which is what makes it hard to spot.
        # utf-8-sig strips a BOM when present and is a no-op otherwise.
        with open(path, encoding="utf-8-sig", errors="strict" if strict else "ignore") as fh:
            return fh.read()
    except UnicodeDecodeError:
        return None
    except OSError as exc:
        print(f"manifest-gen: WARN: cannot read {path}: {exc}", file=sys.stderr)
        return None


def build_records(root: str, cfg: dict) -> list:
    """[(reldir, absdir, [(relfile, [Decl, ...]), ...], n_decls), ...] for every
    directory holding >=1 indexed declaration."""
    scanners = make_scanner_map(cfg)
    gen_names = {cfg["root_manifest"], cfg["submanifest_name"]}
    gen_res = [re.compile(p) for p in cfg["generated_basenames"]]
    records = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = _filter_dirs(dirnames, dirpath, root, cfg)
        files = []
        for fn in sorted(filenames):
            if fn in gen_names or any(r.search(fn) for r in gen_res):
                continue
            scanner = scanners.get(os.path.splitext(fn)[1].lower())
            if scanner is None:
                continue
            ap = os.path.join(dirpath, fn)
            rp = _rel(ap, root)
            if _ignored_path(rp, cfg):
                continue
            src = _read_source(ap, cfg)
            if src is None:
                continue
            decls = sorted(scanner(src), key=lambda d: (d[0], d[2], d[1]))
            files.append((rp, decls))
        n = sum(len(d) for _, d in files)
        if n:
            records.append((_rel(dirpath, root), dirpath, files, n))
    records.sort(key=lambda r: r[0])
    return records


# --------------------------------------------------------------- rendering --

def _decl_line(d: Decl, cfg: dict) -> str:
    line, name, kind, ctx = d
    if cfg["include_context"]:
        ctx = cfg["context_overrides"].get(kind, ctx)
        return f"- L{line}: `{name}` ({kind}; context: {ctx})"
    return f"- L{line}: `{name}` ({kind})"


def _render_lines(lines: list[str]) -> str:
    while lines and lines[-1] == "":
        lines.pop()
    return "\n".join(lines) + "\n"


def render_submanifest(cfg: dict, rd: str, files: list, n: int) -> str:
    named = "Named declarations" if cfg["named_wording"] else "Declarations"
    empty = ("- No named declarations indexed." if cfg["named_wording"]
             else "- No indexed declarations.")
    lines = [
        "# Function Submanifest", "",
        f"Generated by `{cfg['generator_label']}`. Do not edit manually.", "",
        f"Directory: `{rd}`",
        f"Source files indexed: {len(files)}",
        f"{named} indexed: {n}", "",
    ]
    if cfg["directory_notes"]:
        lines += ["## Directory Notes", ""]
        lines += [f"- {note}" for note in cfg["directory_notes"]]
        lines.append("")
    lines += ["## Files", ""]
    for rp, decls in files:
        lines += [f"### `{os.path.basename(rp)}`", f"Path: `{rp}`"]
        if not decls:
            lines += [empty, ""]
            continue
        lines += [_decl_line(d, cfg) for d in decls]
        lines.append("")
    return _render_lines(lines)


def render_root(cfg: dict, records: list) -> str:
    nf = sum(len(f) for _, _, f, _ in records)
    nd = sum(n for *_, n in records)
    named = "named declarations" if cfg["named_wording"] else "declarations"
    lines = [
        "# Function Manifest", "",
        f"Generated by `{cfg['generator_label']}`. Do not edit manually.", "",
        cfg["description"], "",
        f"Indexed directories: {len(records)}",
        f"Indexed source files: {nf}",
        f"Indexed {named}: {nd}", "",
    ]
    if cfg["coverage_rules"]:
        lines += ["## Coverage Rules", ""]
        lines += [f"- {rule}" for rule in cfg["coverage_rules"]]
        lines.append("")
    lines += ["## Directory Index", ""]
    sub = cfg["submanifest_name"]
    for rd, _, files, n in records:
        sp = sub if rd == "." else f"{rd}/{sub}"
        lines.append(f"- [`{rd}`](./{sp}): {len(files)} source files, {n} {named}.")
    lines.append("")
    return _render_lines(lines)


# ------------------------------------------------- Python registry (ast) ----

def _fmt_args(node) -> str:
    """Deterministic signature rendering (ported from mms gen_py_registry.py)."""
    a = node.args
    parts: list[str] = []
    posonly = list(getattr(a, "posonlyargs", []))
    parts.extend(arg.arg for arg in posonly)
    if posonly:
        parts.append("/")
    first_default = len(posonly) + len(a.args) - len(a.defaults)
    for i, arg in enumerate(a.args):
        idx = len(posonly) + i
        parts.append(f"{arg.arg}=..." if idx >= first_default else arg.arg)
    if a.vararg:
        parts.append(f"*{a.vararg.arg}")
    elif a.kwonlyargs:
        parts.append("*")
    for i, arg in enumerate(a.kwonlyargs):
        has_default = i < len(a.kw_defaults) and a.kw_defaults[i] is not None
        parts.append(f"{arg.arg}=..." if has_default else arg.arg)
    if a.kwarg:
        parts.append(f"**{a.kwarg.arg}")
    return ", ".join(parts)


def _decorator_strs(node) -> list[str]:
    out = []
    for d in node.decorator_list:
        try:
            out.append("@" + ast.unparse(d))
        except Exception:  # noqa: BLE001 - any unparse failure degrades gracefully
            out.append("@<decorator>")
    return out


def _registry_scan(src: str, filename: str):
    """Return ((funcs, classes, vars), None) or (None, SyntaxError)."""
    try:
        tree = ast.parse(src, filename=filename)
    except SyntaxError as exc:
        return None, exc
    funcs, classes, variables = [], [], []
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            kind = "async def" if isinstance(node, ast.AsyncFunctionDef) else "def"
            funcs.append({"name": node.name, "line": node.lineno,
                          "sig": f"{kind} {node.name}({_fmt_args(node)})",
                          "decorators": _decorator_strs(node)})
        elif isinstance(node, ast.ClassDef):
            bases = []
            for b in node.bases:
                try:
                    bases.append(ast.unparse(b))
                except Exception:  # noqa: BLE001
                    bases.append("<base>")
            methods = [{"name": s.name, "line": s.lineno,
                        "sig": ("async def " if isinstance(s, ast.AsyncFunctionDef) else "def ")
                               + f"{s.name}({_fmt_args(s)})"}
                       for s in node.body
                       if isinstance(s, (ast.FunctionDef, ast.AsyncFunctionDef))]
            classes.append({"name": node.name, "line": node.lineno, "bases": bases,
                            "methods": methods, "decorators": _decorator_strs(node)})
        elif isinstance(node, ast.Assign):
            variables.extend({"name": t.id, "line": node.lineno}
                             for t in node.targets if isinstance(t, ast.Name))
        elif isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
            variables.append({"name": node.target.id, "line": node.lineno})
    return (funcs, classes, variables), None


def _render_registry_file(out: list, rel: str, data) -> None:
    out.append(f"### `{rel}`")
    if data is None:
        out += ["- (parse error — file skipped)", ""]
        return
    funcs, classes, variables = data
    if not (funcs or classes or variables):
        out += ["- (no module-level declarations)", ""]
        return
    for c in classes:
        base = f"({', '.join(c['bases'])})" if c["bases"] else ""
        deco = (" " + " ".join(c["decorators"])) if c["decorators"] else ""
        out.append(f"- **class** `{c['name']}{base}` — L{c['line']}{deco}")
        out.extend(f"    - `{m['sig']}` — L{m['line']}" for m in c["methods"])
    for f in funcs:
        deco = (" " + " ".join(f["decorators"])) if f["decorators"] else ""
        out.append(f"- `{f['sig']}` — L{f['line']}{deco}")
    if variables:
        names = ", ".join(f"`{v['name']}` (L{v['line']})" for v in variables)
        out.append(f"- _module vars:_ {names}")
    out.append("")


def build_py_registry(root: str, cfg: dict) -> str:
    rows, totals = [], {"files": 0, "funcs": 0, "classes": 0, "methods": 0, "vars": 0}
    n_errors = 0
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = _filter_dirs(dirnames, dirpath, root, cfg)
        for fn in sorted(filenames):
            if not fn.endswith(".py") or fn.startswith("._"):
                continue
            ap = os.path.join(dirpath, fn)
            src = _read_source(ap, cfg)
            if src is None:
                continue
            totals["files"] += 1
            data, err = _registry_scan(src, ap)
            if err is not None:
                n_errors += 1
                rows.append((_rel(ap, root), None))
                continue
            funcs, classes, variables = data
            totals["funcs"] += len(funcs)
            totals["classes"] += len(classes)
            totals["methods"] += sum(len(c["methods"]) for c in classes)
            totals["vars"] += len(variables)
            rows.append((_rel(ap, root), data))
    out = [
        "# Python Function / Class / Variable Registry", "",
        f"Deterministic registry of all `.py` sources, generated by "
        f"`{cfg['generator_label']}` (stdlib `ast`). Do not edit by hand.", "",
        f"- Python files indexed: **{totals['files']}**",
        f"- Functions (module-level): **{totals['funcs']}**",
        f"- Classes: **{totals['classes']}**  (methods: **{totals['methods']}**)",
        f"- Module-level variables/constants: **{totals['vars']}**",
    ]
    if n_errors:
        out.append(f"- Files with parse errors: **{n_errors}**")
    out += ["", "## Files", ""]
    for rel_path, data in rows:
        _render_registry_file(out, rel_path, data)
    return "\n".join(out) + "\n"


# ------------------------------------------------------- file inventory -----

_INV_LANG = {".ps1": "powershell", ".psm1": "powershell", ".sh": "shell",
             ".py": "python", ".json": "json", ".md": "markdown",
             ".bat": "batch", ".cmd": "batch", ".txt": "text", ".tsv": "tsv",
             ".js": "javascript", ".mjs": "javascript", ".ts": "typescript",
             ".sql": "sql", ".c": "c", ".h": "c", ".go": "go", ".rs": "rust"}


def _inv_component(rel: str, name: str, inv: dict) -> str:
    for prefix, comp in inv.get("component_rules", []):
        if rel == prefix.rstrip("/") or rel.startswith(prefix):
            return comp
    for suffix, comp in inv.get("suffix_components", []):
        if name.endswith(suffix):
            return comp
    return inv.get("default_component", "other")


def _inv_role(name: str, inv: dict) -> str:
    lowered = name.lower()
    for prefix, role in inv.get("role_prefix_rules", []):
        if lowered.startswith(prefix.lower()):
            return role
    return inv.get("default_role", "support")


def build_inventory(root: str, cfg: dict) -> dict:
    inv = cfg["inventory"]
    ignored = set(inv.get("ignored_dirs", cfg["ignored_dirs"]))
    json_dir = inv.get("json_dir", ".manifests-json").rstrip("/")
    lang_map = {**_INV_LANG, **inv.get("lang_map", {})}
    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in ignored and not d.startswith("._"))
        for fn in sorted(filenames):
            if fn.startswith("._") or fn == ".DS_Store":
                continue
            ap = os.path.join(dirpath, fn)
            rp = _rel(ap, root)
            if rp == json_dir or rp.startswith(json_dir + "/"):
                continue
            try:
                with open(ap, "rb") as fh:
                    data = fh.read()
            except OSError:
                continue
            ext = os.path.splitext(fn)[1].lower()
            lines = data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)
            files.append({"path": rp, "component": _inv_component(rp, fn, inv),
                          "lang": lang_map.get(ext, "other"), "bytes": len(data),
                          "sha256": hashlib.sha256(data).hexdigest(), "lines": lines,
                          "role": _inv_role(fn, inv)})
    files.sort(key=lambda f: (f["component"], f["path"]))
    return {"root": os.path.abspath(root), "file_count": len(files),
            "total_bytes": sum(f["bytes"] for f in files),
            "components": sorted({f["component"] for f in files}), "files": files}


def write_inventory(base: str, cfg: dict, manifest: dict) -> str:
    json_dir = os.path.join(base, cfg["inventory"].get("json_dir", ".manifests-json"))
    os.makedirs(json_dir, exist_ok=True)
    _write(os.path.join(json_dir, "MANIFEST.json"), json.dumps(manifest, indent=1))
    for comp in manifest["components"]:
        sub = [f for f in manifest["files"] if f["component"] == comp]
        payload = {"component": comp, "file_count": len(sub),
                   "total_bytes": sum(f["bytes"] for f in sub), "files": sub}
        _write(os.path.join(json_dir, f"submanifest_{comp}.json"), json.dumps(payload, indent=1))
    return json_dir


# ------------------------------------------------------------------ output --

def _write(path: str, text: str) -> None:
    try:
        parent = os.path.dirname(path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
    except OSError as exc:
        die(f"cannot write {path}: {exc}", 1)


def prune_stale(root: str, cfg: dict, active: set) -> int:
    removed = 0
    sub = cfg["submanifest_name"]
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = _filter_dirs(dirnames, dirpath, root, cfg)
        if sub in filenames and _rel(dirpath, root) not in active:
            try:
                os.unlink(os.path.join(dirpath, sub))
                removed += 1
            except OSError as exc:
                print(f"manifest-gen: WARN: cannot prune {dirpath}/{sub}: {exc}",
                      file=sys.stderr)
    return removed


def write_outputs(root: str, cfg: dict, records: list, outdir: str | None) -> None:
    base = outdir or root
    for rd, _, files, n in records:
        tdir = base if rd == "." else os.path.join(base, rd)
        _write(os.path.join(tdir, cfg["submanifest_name"]),
               render_submanifest(cfg, rd, files, n))
    _write(os.path.join(base, cfg["root_manifest"]), render_root(cfg, records))


# -------------------------------------------------------------------- main --

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        prog="manifest-gen.py",
        description="Canonical FUNCTION_MANIFEST/FUNCTION_SUBMANIFEST generator "
                    "(manifest-kit). Config: ROOT/manifest.config.json.")
    ap.add_argument("root", nargs="?", default=".", help="repo root (default: cwd)")
    ap.add_argument("--config", help="explicit config path (default: ROOT/manifest.config.json)")
    ap.add_argument("--output-dir", help="mirror all writes into DIR instead of ROOT "
                                         "(read-only verification; disables pruning)")
    ap.add_argument("--print-config", action="store_true",
                    help="print the effective merged config and exit")
    ap.add_argument("--version", action="version", version=f"manifest-kit {KIT_VERSION}")
    args = ap.parse_args(argv)

    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        die(f"root is not a directory: {root}", 2)
    cfg = load_config(root, args.config)
    if args.print_config:
        json.dump(cfg, sys.stdout, indent=2)
        print()
        return 0

    records = build_records(root, cfg)
    outdir = os.path.abspath(args.output_dir) if args.output_dir else None
    pruned = 0
    if outdir is None and cfg["prune_stale"]:
        pruned = prune_stale(root, cfg, {r[0] for r in records})
    write_outputs(root, cfg, records, outdir)

    nd = sum(n for *_, n in records)
    nf = sum(len(f) for _, _, f, _ in records)
    extras = []
    if cfg["py_registry"]:
        _write(os.path.join(outdir or root, cfg["py_registry"]), build_py_registry(root, cfg))
        extras.append(cfg["py_registry"])
    if cfg["inventory"]:
        write_inventory(outdir or root, cfg, build_inventory(root, cfg))
        extras.append(cfg["inventory"].get("json_dir", ".manifests-json") + "/MANIFEST.json")
    msg = (f"Wrote {len(records)} submanifests + {cfg['root_manifest']} "
           f"({nd} declarations across {nf} files)")
    if extras:
        msg += " + " + ", ".join(extras)
    if pruned:
        msg += f"; pruned {pruned} stale submanifests"
    print(msg + ".")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
