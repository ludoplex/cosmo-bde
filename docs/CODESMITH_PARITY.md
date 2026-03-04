# CodeSmith Parity Harness (PR1)

This document defines the PR1 foundation for reverse-engineering and parity
tracking against `Generator-85.zip`.

## Scope

PR1 intentionally does not implement template execution. It delivers:

- deterministic corpus inventory lock
- deterministic corpus extraction
- execution harness scaffold (dry-run + baseline plumbing)

## Commands

```bash
make codesmith-corpus-lock CODESMITH_ZIP="$HOME/Downloads/Generator-85.zip"
make codesmith-corpus CODESMITH_ZIP="$HOME/Downloads/Generator-85.zip"
make codesmith-parity CODESMITH_ZIP="$HOME/Downloads/Generator-85.zip"
```

On Windows, prefer:

```bash
make codesmith-corpus CODESMITH_ZIP="C:/Users/<you>/Downloads/Generator-85.zip"
```

`codesmith-parity` defaults to dry-run (fixture discovery and corpus integrity).

To execute an engine command per fixture:

```bash
make codesmith-parity CODESMITH_ZIP="$HOME/Downloads/Generator-85.zip" \
  ENGINE="cat {input}"
```

## Lock File

`specs/testing/codesmith/corpus.lock.json` captures:

- source archive hash
- nested sample archive scope
- file extension filters
- deterministic entry list with size + sha256

The lock file is used to guarantee fixture selection stability across machines
and future parity PRs.

## Harness Outputs

When engine mode is used, artifacts are written under:

- `build/codesmith/parity/**/*.stdout`
- `build/codesmith/parity/**/*.stderr`
- `build/codesmith/parity/**/*.exitcode`

These are intentionally outside version control.
