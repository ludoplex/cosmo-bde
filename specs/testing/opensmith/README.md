# OpenSmith Parity Corpus (PR1)

This directory stores the deterministic corpus lock for reverse-engineering
the `Generator-85.zip` template ecosystem.

## Why a lock file?

- Keeps fixture discovery deterministic across machines.
- Avoids committing proprietary binaries or full archive dumps.
- Enables reproducible parity runs against the same sample set.

## Inputs

- Primary archive: `Generator-85.zip`
- Nested sample archives scanned by default: `Samples/*.zip`
- Included file types:
  - `.cst`
  - `.csp`
  - `.csmap`
  - `.xsd`
  - `.xml`
  - `.json`

## Workflow

1. Generate/update lock file:

```bash
make opensmith-corpus-lock OPENSMITH_ZIP="$HOME/Downloads/Generator-85.zip"
```

2. Extract corpus for local harness runs:

```bash
make opensmith-corpus OPENSMITH_ZIP="$HOME/Downloads/Generator-85.zip"
```

3. Run dry-run parity harness (fixture discovery check):

```bash
make opensmith-parity OPENSMITH_ZIP="$HOME/Downloads/Generator-85.zip"
```

4. Build the parser front-end as APE (`build/opensmithgen.com`):

```bash
make opensmithgen-ape
```

5. Run parser/AST front-end roundtrip check (`opensmithgen`) on corpus fixtures:

```bash
make opensmith-frontend-check OPENSMITH_ZIP="$HOME/Downloads/Generator-85.zip"
```

6. Run with an engine command (command receives each fixture path):

```bash
make opensmith-parity OPENSMITH_ZIP="$HOME/Downloads/Generator-85.zip" \
  ENGINE="cat {input}"
```

Windows path example:

```bash
make opensmith-corpus OPENSMITH_ZIP="C:/Users/<you>/Downloads/Generator-85.zip"
```
