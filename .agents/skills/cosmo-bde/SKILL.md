```markdown
# cosmo-bde Development Patterns

> Auto-generated skill from repository analysis

## Overview

This skill teaches you the core development patterns, coding conventions, and collaborative workflows used in the `cosmo-bde` TypeScript codebase. The repository emphasizes strong code generation practices, modular schema-driven design, and careful management of vendor dependencies and CI/CD pipelines. You'll learn how to add new domain schemas, implement code generators, manage generated files, update submodules, and keep documentation and CI in sync—all following established conventions and using suggested `/commands` for common tasks.

---

## Coding Conventions

### File Naming

- **Style:** `snake_case`
- **Example:**  
  ```
  user_profile.ts
  domain_schema.test.ts
  ```

### Import Style

- **Relative imports** are used throughout.
- **Example:**
  ```typescript
  import { parseUser } from './user_parser';
  ```

### Export Style

- **Named exports** are preferred.
- **Example:**
  ```typescript
  export function validateSchema(schema: object): boolean { ... }
  export const DEFAULT_TIMEOUT = 5000;
  ```

### Commit Messages

- **Conventional commits** are used.
- **Prefixes:** `chore`, `feat`, `fix`, `docs`, `refactor`
- **Example:**
  ```
  feat: add support for new domain schema
  fix: correct SQL generation for nested types
  docs: update README with generator usage
  ```

---

## Workflows

### Add New Domain Schema and Generate Types

**Trigger:** When introducing a new data model or protocol type.  
**Command:** `/add-schema`

1. Create or update a schema file in `specs/domain/*.schema`.
2. Run code generators to produce all corresponding files:
    - C types: `gen/domain/*_types.c`, `gen/domain/*_types.h`
    - JSON: `gen/domain/*_json.c`, `gen/domain/*_json.h`
    - SQL: `gen/domain/*_sql.c`, `gen/domain/*_sql.h`
    - Protocol Buffers: `gen/domain/*.proto`
    - FlatBuffers: `gen/domain/*.fbs`
3. Commit both the new/updated schema and all generated files.

**Example:**
```bash
# Add schema
vim specs/domain/order.schema

# Generate code
./scripts/gen-domain.sh

# Stage and commit
git add specs/domain/order.schema gen/domain/order_*
git commit -m "feat: add order domain schema and generated types"
```

---

### Implement New Generator Tool

**Trigger:** When automating code generation for a new spec format or workflow.  
**Command:** `/new-generator`

1. Add tool source: `tools/{genname}/{genname}.c`
2. Add supporting files: `tools/{genname}/{genname}_self.h`, `{genname}_tokens.def`
3. Add an example spec in `specs/{domain|behavior|interface|platform}/*.ext`
4. Run the generator to produce files in `gen/{domain|behavior|api|platform}/*`
5. Update `Makefile` and/or scripts as needed.
6. Commit all new/modified files.

**Example:**
```bash
# Add generator source
vim tools/hsmgen/hsmgen.c

# Add example spec
vim specs/behavior/state_machine.ext

# Generate code
make hsmgen

# Commit changes
git add tools/hsmgen/* specs/behavior/state_machine.ext gen/behavior/*
git commit -m "feat: implement HSM generator and add example spec"
```

---

### Regenerate All Generated Files

**Trigger:** After changing a generator, schema, or spec to update all outputs.  
**Command:** `/regen-all`

1. Run `scripts/regen-all.sh` or the equivalent script.
2. Update `gen/*/GENERATOR_VERSION` and `gen/REGEN_TIMESTAMP`.
3. Update all affected generated files in `gen/`.
4. Commit all regenerated files.

**Example:**
```bash
./scripts/regen-all.sh
git add gen/
git commit -m "chore: regenerate all generated files after schema update"
```

---

### Update or Add Vendor Submodule

**Trigger:** When adding a new external dependency or updating an existing one.  
**Command:** `/update-vendor`

1. Add or update the entry in `.gitmodules`.
2. Add or update `vendors/submodules/{submodule}`.
3. Update related documentation (`VENDORS.md`, `TOOLING.md`, etc.) if needed.
4. Commit all changes.

**Example:**
```bash
git submodule add https://github.com/example/tool vendors/submodules/tool
git add .gitmodules vendors/submodules/tool
vim VENDORS.md
git commit -m "chore: add tool as vendor submodule"
```

---

### CI Workflow Update

**Trigger:** When improving or fixing CI/CD behavior.  
**Command:** `/update-ci`

1. Edit `.github/workflows/*.yml` or `.github/actions/*`.
2. Edit `scripts/test.sh`, `scripts/verify.sh`, or related scripts.
3. Commit changes.

**Example:**
```bash
vim .github/workflows/ci.yml
vim scripts/test.sh
git commit -am "fix: update CI workflow to handle submodules"
```

---

### Docs Update with Generated File Drift

**Trigger:** When updating documentation and keeping generated file metadata in sync.  
**Command:** `/docs-update`

1. Edit documentation files (`README.md`, `.claude/CLAUDE.md`, `VENDORS.md`, etc).
2. Update `gen/*/GENERATOR_VERSION` as needed.
3. Commit all changes.

**Example:**
```bash
vim README.md
vim gen/domain/GENERATOR_VERSION
git commit -am "docs: update README and generator version"
```

---

## Testing Patterns

- **Test files** use the pattern: `*.test.*`
- **Testing framework:** Not explicitly detected; check for custom or standard JS/TS frameworks.
- **Example:**
  ```
  user_parser.test.ts
  domain_schema.test.ts
  ```
- **Typical test structure:**
  ```typescript
  import { validateSchema } from './domain_schema';

  describe('validateSchema', () => {
    it('returns true for valid schema', () => {
      expect(validateSchema({ ... })).toBe(true);
    });
  });
  ```

---

## Commands

| Command        | Purpose                                                        |
|----------------|----------------------------------------------------------------|
| /add-schema    | Add a new domain schema and generate all corresponding types   |
| /new-generator | Implement a new code generator tool and add example/spec files |
| /regen-all     | Regenerate all generated files after changes                   |
| /update-vendor | Add or update a vendor/submodule                              |
| /update-ci     | Update CI workflow files and related scripts                   |
| /docs-update   | Update documentation and generator version metadata            |
```
