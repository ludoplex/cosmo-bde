# Vendored Dependencies

Single-file libraries vendored for Ring 0 self-sufficiency.
No external package manager required - just C + sh + make.

## Contents

| Library | Version | Purpose | License |
|---------|---------|---------|---------|
| yyjson | 0.10.0 | JSON serialization for `*_json.c` | MIT |

## SQLite is not vendored

Generated `*_sql.c` bindings use `#include <sqlite3.h>`. Cosmopolitan already
ships SQLite as `third_party/sqlite3` in
[jart/cosmopolitan](https://github.com/jart/cosmopolitan/tree/master/third_party/sqlite3),
so cosmo-bde reuses that copy instead of carrying its own amalgamation:

- `vendors/submodules/cosmopolitan` is pinned to the commit the pinned
  `cosmocc` release was built from (`cosmocc.mk`: `COSMO_SRC_COMMIT`).
- `make cosmo-src` does a sparse, shallow checkout of `third_party/sqlite3`.
- `make sqlite3` builds `build/libsqlite3.a` with cosmocc, using the compile
  flags from that directory's `BUILD.mk`.
- `make check` round-trips the generated bindings through it
  (`test/sql_test.c`). Native builds (`make CC=cc check`) link the host's
  `libsqlite3` instead.

## Usage

```c
#include <yyjson.h>   // JSON serialization
#include <sqlite3.h>  // SQL persistence (cosmopolitan third_party/sqlite3)
```

Build with: `cc -Ivendors/libs ...`

## Updates

```sh
# yyjson
curl -sLO https://raw.githubusercontent.com/ibireme/yyjson/master/src/yyjson.h
curl -sLO https://raw.githubusercontent.com/ibireme/yyjson/master/src/yyjson.c
```

## Cosmopolitan Compatibility

yyjson compiles cleanly with cosmocc for APE portability.
