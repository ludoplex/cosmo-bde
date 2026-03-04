# OpenSmith Parity Harness (PR1)

This document defines the PR1/PR2 foundation for reverse-engineering and parity
tracking against `Generator-85.zip`.

## Scope

Current scope intentionally does not implement template execution. It delivers:

- deterministic corpus inventory lock
- deterministic corpus extraction
- execution harness scaffold (dry-run + baseline plumbing)
- parser/AST front-end scaffold (`opensmithgen`) with roundtrip checks

## Commands

```bash
make opensmith-corpus-lock OPENSMITH_ZIP="$HOME/Downloads/Generator-85.zip"
make opensmith-corpus OPENSMITH_ZIP="$HOME/Downloads/Generator-85.zip"
make opensmith-parity OPENSMITH_ZIP="$HOME/Downloads/Generator-85.zip"
make opensmithgen-ape
make opensmith-frontend-check OPENSMITH_ZIP="$HOME/Downloads/Generator-85.zip"
```

On Windows, prefer:

```bash
make opensmith-corpus OPENSMITH_ZIP="C:/Users/<you>/Downloads/Generator-85.zip"
```

`opensmith-parity` defaults to dry-run (fixture discovery and corpus integrity).

To execute an engine command per fixture:

```bash
make opensmith-parity OPENSMITH_ZIP="$HOME/Downloads/Generator-85.zip" \
  ENGINE="cat {input}"
```

To run parser/AST roundtrip checks over all `.cst/.csp/.csmap` fixtures:

```bash
make opensmith-frontend-check OPENSMITH_ZIP="$HOME/Downloads/Generator-85.zip"
```

`opensmith-frontend-check` now uses the APE build script:

- `scripts/build_opensmithgen_ape.py`
- output binary: `build/opensmithgen.com`

## Lock File

`specs/testing/opensmith/corpus.lock.json` captures:

- source archive hash
- nested sample archive scope
- file extension filters
- deterministic entry list with size + sha256

The lock file is used to guarantee fixture selection stability across machines
and future parity PRs.

## Harness Outputs

When engine mode is used, artifacts are written under:

- `build/opensmith/parity/**/*.stdout`
- `build/opensmith/parity/**/*.stderr`
- `build/opensmith/parity/**/*.exitcode`

These are intentionally outside version control.
