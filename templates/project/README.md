# cosmo-bde Project Template

Spec-driven C development with live reload.

## Quick Start

```bash
# 1. Edit specs
vim specs/domain/app.schema

# 2. Regenerate code
make regen

# 3. Build
make

# 4. Run with live reload (edit code, see changes instantly)
make livereload
```

## Directory Structure

```
specs/
  domain/           # Type definitions (.schema)
  behavior/         # State machines (.sm), BDD (.feature)
gen/                # Generated code (DO NOT EDIT)
src/                # Hand-written implementation
build/              # Build artifacts
```

## Workflow

```
Edit spec  →  make regen  →  make verify  →  make  →  commit
```

## Live Reload

Hot-patch your running binary in real-time:

```bash
# Terminal 1: Run your app
./build/app

# Terminal 2: Attach live reload
sudo make livereload

# Terminal 3: Edit src/*.c, save
# Watch Terminal 1 - changes appear instantly!
```

## Adding New Types

1. Create `specs/domain/mytype.schema`:
   ```
   type MyType {
       name: string[64]
       value: i32
   }
   ```

2. Run `make regen` - generates:
   - `gen/domain/mytype_types.h` - C structs
   - `gen/domain/mytype_types.c` - init/validate functions

3. Include in your code:
   ```c
   #include "mytype_types.h"
   ```

## Requirements

- cosmocc (Cosmopolitan C compiler)
- make

Install cosmocc:
```bash
# pinned release; verify before unpacking
curl -fsSLo /tmp/cosmocc-4.0.2.zip https://cosmo.zip/pub/cosmocc/cosmocc-4.0.2.zip
echo "85b8c37a406d862e656ad4ec14be9f6ce474c1b436b9615e91a55208aced3f44  /tmp/cosmocc-4.0.2.zip" | shasum -a 256 -c -
mkdir -p ~/.cosmo && unzip -q /tmp/cosmocc-4.0.2.zip -d ~/.cosmo
export PATH="$HOME/.cosmo/bin:$PATH"
```
