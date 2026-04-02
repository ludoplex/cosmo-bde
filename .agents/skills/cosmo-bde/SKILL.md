```markdown
# cosmo-bde Development Patterns

> Auto-generated skill from repository analysis

## Overview

This skill introduces the core development patterns, coding conventions, and automated workflows used in the `cosmo-bde` TypeScript codebase. The repository is focused on code generation from domain-specific specs, behavioral models, BDD features, and API definitions, producing C code and related artifacts. The project emphasizes consistency in file structure, naming, and commit practices, and leverages a suite of generator tools for rapid, reproducible development.

## Coding Conventions

- **Language:** TypeScript (no framework detected)
- **File Naming:** Use `snake_case` for all file and directory names.
  - Example: `user_profile.ts`, `domain_schema.ts`
- **Imports:** Use relative import paths.
  - Example:
    ```typescript
    import { parseSchema } from './schema_parser';
    ```
- **Exports:** Use named exports.
  - Example:
    ```typescript
    // Good
    export function generateTypes() { ... }

    // Avoid default exports
    // export default function() { ... }
    ```
- **Commit Messages:** Follow [Conventional Commits](https://www.conventionalcommits.org/) with prefixes like `feat`, `fix`, `chore`, `docs`, `refactor`.
  - Example: `feat: add user profile domain schema`
- **Generated Code:** Always commit both the source spec and all generated files.

## Workflows

### Add or Update Domain Schema and Generate Types
**Trigger:** When adding or updating a data model/domain schema  
**Command:** `/add-schema`

1. Edit or add a schema file in `specs/domain/*.schema`.
2. Run code generation:
   ```sh
   make regen
   # or
   scripts/regen-all.sh
   ```
3. Generated files will appear in `gen/domain/`:
   - `*_types.c`, `*_types.h`
   - `*_json.c`, `*_json.h`
   - `*_sql.c`, `*_sql.h`
   - `*.proto`, `*.fbs`
4. Commit both the updated schema and all generated files.

---

### Add or Update Behavioral Spec and Generate State Machine
**Trigger:** When defining or modifying a state machine for behavioral logic  
**Command:** `/add-state-machine`

1. Edit or add a spec in `specs/behavior/*.hsm`, `*.msm`, or `*.sm`.
2. Run code generation:
   ```sh
   make regen
   # or
   scripts/regen-all.sh
   ```
3. Generated files will appear in `gen/behavior/`:
   - `*_hsm.c`, `*_hsm.h`
   - `*_msm.c`, `*_msm.h`
4. Commit both the updated spec and all generated files.

---

### Add or Update BDD Feature and Generate BDD Tests
**Trigger:** When adding or modifying BDD tests for a feature  
**Command:** `/add-bdd-feature`

1. Edit or add a `.feature` file in `specs/testing/*.feature`.
2. Run code generation:
   ```sh
   make regen
   # or
   scripts/regen-all.sh
   ```
3. Generated files will appear in `gen/testing/`:
   - `*_bdd.c`, `*_bdd.h`
   - `*_steps.c`
4. Commit both the updated feature file and all generated files.

---

### Add or Update API Spec and Generate API Bindings
**Trigger:** When adding or modifying a REST API endpoint or service  
**Command:** `/add-api-endpoint`

1. Edit or add an API spec in `specs/api/*.api`.
2. Run code generation:
   ```sh
   make regen
   # or
   scripts/regen-all.sh
   ```
3. Generated files will appear in `gen/api/`:
   - `*_api.c`, `*_api.h`
4. Commit both the updated API spec and all generated files.

---

### Generator Tool Implementation and Dogfooding
**Trigger:** When adding a new code generator tool and validating it with a real spec  
**Command:** `/add-generator`

1. Add new generator source files to `tools/<genname>/`.
2. Add self-hosting tokens/defs as needed (e.g., `*_self.h`, `*_tokens.def`).
3. Add a matching spec file in `specs/<domain>/`.
4. Run code generation:
   ```sh
   make regen
   # or
   scripts/regen-all.sh
   ```
5. Commit the tool, the spec, and all generated outputs.

---

### Update Generator Version and Regenerate All
**Trigger:** When ensuring all generated code is up-to-date after tool or spec changes  
**Command:** `/regen-all`

1. Run code generation:
   ```sh
   make regen
   # or
   scripts/regen-all.sh
   ```
2. `GENERATOR_VERSION` and `REGEN_TIMESTAMP` files are updated in:
   - `gen/api/`, `gen/behavior/`, `gen/domain/`, `gen/testing/`
3. Commit the updated version/timestamp files and any other regenerated outputs.

---

### Update Vendor or Submodule Directory Structure
**Trigger:** When adding, updating, or reorganizing third-party dependencies or submodules  
**Command:** `/update-vendors`

1. Edit the `vendors/` and/or `upstream/` directory structure as needed.
2. Update `.gitmodules` and related documentation (`VENDORS.md`, `.claude/CLAUDE.md`).
3. Commit all moved files, updated submodules, and doc changes.

---

## Testing Patterns

- **Test Files:** Use the pattern `*.test.*` for test files.
  - Example: `user_profile.test.ts`
- **Testing Framework:** Not explicitly detected; check project documentation or package.json for details.
- **Test Location:** Co-locate test files with the code they test or in a dedicated test directory.

Example test file:
```typescript
import { parseSchema } from './schema_parser';

describe('parseSchema', () => {
  it('parses valid schema', () => {
    // test logic here
  });
});
```

## Commands

| Command            | Purpose                                                      |
|--------------------|-------------------------------------------------------------|
| /add-schema        | Add or update a domain schema and generate all related types |
| /add-state-machine | Add or update a behavioral spec and generate state machine   |
| /add-bdd-feature   | Add or update a BDD feature and generate BDD tests          |
| /add-api-endpoint  | Add or update an API spec and generate API bindings         |
| /add-generator     | Implement a new generator tool and dogfood it               |
| /regen-all         | Regenerate all code and update generator version/timestamp   |
| /update-vendors    | Update vendor or submodule directory structure               |
```
