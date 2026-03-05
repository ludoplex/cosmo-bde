# cosmo-bde Specification Types

> **BDE with Models** — Behavior Driven Engineering with Models.
>
> Every line of C should trace back to a spec.

## Spec File Inventory

| Extension | Generator | Purpose | Ring | Status |
|-----------|-----------|---------|------|--------|
| `.schema` | schemagen | Data types, structs, validation | 0 | ✓ Working + Self-Hosted |
| `.def` | defgen | Constants, enums, X-Macros, config | 0 | ✓ Working |
| `.impl` | implgen | Platform hints, SIMD, allocation | 0 | ✓ Working |
| `.sm` | smgen | Flat state machines | 0 | ✓ Working |
| `.hsm` | hsmgen | Hierarchical state machines (UML) | 0 | ✓ Working |
| `.msm` | msmgen | Modal state machines (mode switching) | 0 | ✓ Working |
| `.grammar` | Lemon | Parser grammar (LALR) | 0 | ✓ Vendored + Grammars Created |
| `.lex` | lexgen | Lexer tokens, patterns | 0 | ✓ Working |
| `.ui` | uigen | UI layouts (Nuklear) | 0 | ✓ Working |
| `.api` | apigen | API contracts, endpoints, RPC | 0 | ✓ Working |
| `.proto` | protobuf-c | Wire protocol, serialization | 2 | ✓ Vendored |
| `.rules` | clipsgen | Business rules, constraints | 0 | ✓ Working |
| `.feature` | bddgen | BDD test scenarios (Gherkin) | 0 | ✓ Working |
| `.sql` | sqlgen | Database schema, queries | 0 | ✓ Working |
| `.sig` | siggen | Function signatures, FFI bindings | 0 | ✓ Working |
| `.cst` | cstgen (planned) | OpenSmith template source | 0/2 | ◐ Corpus + parity harness |
| `.csp` | cspgen (planned) | OpenSmith template project orchestration | 0/2 | ◐ Corpus + parity harness |
| `.csmap` | csmapgen (planned) | OpenSmith mapping rules | 0/2 | ◐ Corpus + parity harness |

**Legend:** ✓ Working | ◐ Spec Ready (needs generator) | ○ Stub | ✗ Missing

## Spec Type Details

### `.schema` - Data Types (WORKING)

```
type Point {
    x: i32
    y: i32
    z: i32 [default: 0]
}
```

Generates: `_types.h`, `_types.c` (struct, init, validate)

---

### `.def` - Definitions (SPEC READY)

```
# Constants
const MAX_CONNECTIONS = 1024
const VERSION = "1.0.0"
const BUFFER_SIZE = 1 << 16    # Expression constants

# Enums with X-Macro support
enum LogLevel [prefix: "LOG_"] {
    DEBUG = 0    # Most verbose
    INFO = 1     # Normal operation
    WARN = 2     # Warning conditions
    ERROR = 3    # Error conditions
}

# Flags (bitmask) with X-Macro support
flags Permissions [prefix: "PERM_"] {
    READ = 0x01
    WRITE = 0x02
    EXEC = 0x04
    ADMIN = 0x80
}

# Config with defaults and ranges
config ServerConfig {
    port: u16 = 8080 [range: 1024..65535]
    workers: u8 = 4 [range: 1..32]
    log_level: LogLevel = INFO
}
```

Generates:
- `_defs.h`: macros, enums, X-Macro tables
- `_defs.c`: config defaults, string converters

**X-Macro Output Example:**
```c
#define LOGLEVEL_XMACRO(X) \
    X(LOG_DEBUG, 0, "DEBUG") \
    X(LOG_INFO,  1, "INFO")  \
    X(LOG_WARN,  2, "WARN")  \
    X(LOG_ERROR, 3, "ERROR")

/* Enum expands from X-Macro */
typedef enum {
#define X(name, val, str) name = val,
    LOGLEVEL_XMACRO(X)
#undef X
} LogLevel;

/* String lookup expands from same X-Macro */
const char* LogLevel_to_string(LogLevel val);
```

See: `docs/XMACROS.md` for full X-Macro documentation.

---

### `.impl` - Implementation Directives

```
# Platform-specific implementations
impl platform {
    linux: "impl/linux.c"
    windows: "impl/windows.c"
    macos: "impl/macos.c"
    cosmo: "impl/cosmo.c"  # APE universal
}

# Optimization hints
impl optimize Point {
    alignment: 16          # SIMD alignment
    pack: true             # No padding
    inline: [add, sub]     # Inline these functions
}

# Memory allocation strategy
impl alloc {
    arena: "scratch_arena"
    pool: "point_pool" [size: 1024]
}

# Vectorization hints
impl simd {
    target: [sse4, avx2, neon]
    fallback: scalar
}
```

Generates: Platform dispatch, optimization pragmas, allocation wrappers

---

### `.sm` - Flat State Machine (STUB EXISTS)

```
machine Blinker {
    initial: Off

    state Off {
        on Toggle -> On
        entry: led_off()
    }

    state On {
        on Toggle -> Off
        on Timeout -> Off
        entry: led_on()
    }
}
```

Generates: `_fsm.h`, `_fsm.c` (state enum, transition table, dispatch)

---

### `.hsm` - Hierarchical State Machine

```
machine TrafficLight {
    initial: Operating.Red

    state Operating {
        initial: Red
        on Malfunction -> Fault

        state Red {
            on Timer -> Green
            entry: show_red()
        }

        state Green {
            on Timer -> Yellow
            entry: show_green()
        }

        state Yellow {
            on Timer -> Red
            entry: show_yellow()
        }
    }

    state Fault {
        on Reset -> Operating
        entry: blink_all()
    }
}
```

Generates: Nested state handling, history, orthogonal regions

---

### `.msm` - Modal State Machine

```
# Modal state machines for mode-based systems
# (e.g., editor modes, vehicle modes, security levels)

modal SystemModes {
    initial: Normal

    mode Normal {
        allows: [read, write, execute]
        on Lockdown -> Secure
        on Maintenance -> Service
    }

    mode Secure {
        allows: [read]
        requires: auth_level >= 3
        on Unlock [auth] -> Normal
    }

    mode Service {
        allows: [read, write, diagnose]
        requires: physical_key
        on Complete -> Normal
    }
}
```

Generates: Mode transitions, capability checks, auth integration

---

### `.lex` - Lexer Tokens (STUB EXISTS)

```
# Token definitions with patterns
KEYWORD_IF      "if"
KEYWORD_ELSE    "else"
KEYWORD_WHILE   "while"
KEYWORD_FOR     "for"
KEYWORD_RETURN  "return"

IDENT           [a-zA-Z_][a-zA-Z0-9_]*
NUMBER_INT      [0-9]+
NUMBER_FLOAT    [0-9]+\.[0-9]+
STRING          \"([^\"\\]|\\.)*\"
CHAR            \'([^\'\\]|\\.)\'

OP_PLUS         "+"
OP_MINUS        "-"
OP_STAR         "*"
OP_SLASH        "/"
OP_EQ           "=="
OP_ASSIGN       "="

WHITESPACE      [ \t\r\n]+  @skip
COMMENT_LINE    //[^\n]*    @skip
COMMENT_BLOCK   /\*.*?\*/   @skip
```

Generates: `_lexer.h`, `_lexer.c` (token enum, DFA tables, lex function)

---

### `.api` - API Contracts

```
api UserService {
    version: "1.0"
    transport: [http, grpc]

    endpoint CreateUser {
        method: POST
        path: "/users"
        request: CreateUserRequest
        response: User
        errors: [InvalidInput, AlreadyExists]
    }

    endpoint GetUser {
        method: GET
        path: "/users/{id}"
        request: GetUserRequest
        response: User
        errors: [NotFound]
    }

    # Types for this API
    type CreateUserRequest {
        name: string [not_empty, max: 100]
        email: string [email]
    }

    type User {
        id: u64
        name: string
        email: string
        created_at: i64
    }
}
```

Generates: Request/response types, routing, validation, client stubs

---

### `.rules` - Business Rules (CLIPS-style)

```
ruleset ValidationRules {
    rule PortInRange {
        when: config.port < 1024 || config.port > 65535
        then: error("Port must be 1024-65535")
        severity: error
    }

    rule WarnHighConnections {
        when: config.max_connections > 10000
        then: warn("High connection count may exhaust resources")
        severity: warning
    }

    rule RequireAuth {
        when: config.expose_admin && !config.require_auth
        then: error("Admin endpoint requires authentication")
        severity: error
    }
}
```

Generates: Validation functions, rule engine integration

---

### `.feature` - BDD Scenarios

```
Feature: User Registration
  As a new user
  I want to create an account
  So that I can access the system

  Scenario: Successful registration
    Given no user exists with email "test@example.com"
    When I register with:
      | name  | email             | password |
      | Alice | test@example.com  | secret   |
    Then the user is created
    And I receive a confirmation email

  Scenario: Duplicate email rejected
    Given a user exists with email "test@example.com"
    When I register with email "test@example.com"
    Then I receive error "Email already registered"
```

Generates: Test harness, step definitions, fixtures

---

### `.sig` - Function Signatures

```
# FFI bindings and function signatures
module libc {
    fn malloc(size: size_t) -> void* [may_fail]
    fn free(ptr: void*) [no_fail]
    fn memcpy(dest: void*, src: void*, n: size_t) -> void*
    fn strlen(s: char*) -> size_t [pure]
}

module openssl {
    fn SHA256(data: u8*, len: size_t, out: u8*) -> u8*
    fn EVP_MD_CTX_new() -> EVP_MD_CTX* [may_fail]
}

# Callbacks
callback EventHandler = fn(event: Event*, ctx: void*) -> i32
callback Comparator = fn(a: void*, b: void*) -> i32 [pure]
```

Generates: Header declarations, wrapper functions, type-safe bindings

---

## Dependency Graph

```
                    .def ─────────────────────────────────────┐
                       │                                       │
                       ▼                                       │
                    .schema ─────────────┬────────────────┐    │
                       │                 │                │    │
                       ▼                 ▼                ▼    ▼
                    .impl            .api             .rules (constants)
                       │                 │                │    │
                       ▼                 ▼                ▼    │
                    .sm/.hsm/.msm    .proto          .feature  │
                       │                 │                │    │
                       └────────────────┬┴────────────────┴────┘
                                        │
                    ┌───────────────────┴───────────────────┐
                    │                                       │
                    ▼                                       ▼
                 .lex ──────────────────────────────► .grammar
                    │                                       │
                    │              ┌────────────────────────┘
                    ▼              ▼
                 lexgen          Lemon
                    │              │
                    ▼              ▼
              *_lexer.{h,c}    *_parser.{h,c}
                    │              │
                    └──────┬───────┘
                           │
                           ▼
                    Generated C Code
```

## The Lemon + Lexgen Pattern

**Every generator that parses specs uses this pattern:**

```
specs/{name}.lex     →  lexgen   →  gen/{name}/{name}_lexer.{h,c}
specs/{name}.grammar →  lemon    →  gen/{name}/{name}_parser.{h,c}
specs/{name}.schema  →  schemagen → gen/{name}/{name}_types.{h,c}
```

**Example: schemagen parsing chain:**
```
schemagen.lex     →  lexgen   →  schemagen_lexer.{h,c}   (tokenizer)
schemagen.grammar →  lemon    →  schemagen_parser.{h,c} (LALR parser)
schemagen.schema  →  schemagen→  schemagen_types.{h,c}  (AST types)
```

**Lemon is Ring 0** - vendored from SQLite, public domain, compiles with cosmocc.

## Parser Generation Files

| Generator | .lex file | .grammar file | Status |
|-----------|-----------|---------------|--------|
| schemagen | schemagen.lex | schemagen.grammar | ✓ Created |
| defgen | def.lex | def.grammar | ✓ Created |
| bddgen | feature.lex | feature.grammar | ✓ Created |
| smgen | sm.lex | sm.grammar | ○ Pending |
| lexgen | lex.lex | lex.grammar | ○ Pending (meta!) |

## Self-Hosting Requirements

| Generator | Must use | From spec | Status |
|-----------|----------|-----------|--------|
| schemagen | SchemaField, SchemaTypeDef | schemagen.schema | ✓ Working |
| defgen | DefConstant, DefEnum, DefFlags | defgen.schema → schemagen | ◐ Spec Ready |
| bddgen | BddFeature, BddScenario, BddStep | bddgen.schema → schemagen | ◐ Spec Ready |
| implgen | ImplPlatform, ImplSimd, ImplAlloc | impl.schema → schemagen | ◐ Spec Ready |
| smgen | SmState, SmTransition | smgen.sm | ○ Pending |
| lexgen | LexToken, LexPattern | lexgen.lex | ○ Pending |
| apigen | ApiEndpoint, ApiType | apigen.api | ✗ Missing |

Every generator eats its own output.

## Spec Files Created

```
strict-purist/specs/
├── schemagen.schema     # ✓ Self-hosting types for schemagen
├── defgen.schema        # ◐ Types for defgen (process with schemagen)
├── bddgen.schema        # ◐ Types for bddgen (process with schemagen)
├── def.schema           # Meta-spec: what .def files contain
├── impl.schema          # Meta-spec: what .impl files contain
├── feature.schema       # Meta-spec: what .feature files contain
└── examples/
    ├── logging.def      # Example .def file
    └── platform.impl    # Example .impl file

.claude/features/
├── schemagen.feature    # BDD tests for schemagen
└── defgen.feature       # BDD tests for defgen
```

## Bootstrap Sequence

```
1. schemagen-bootstrap (hand-coded) → processes schemagen.schema
2. schemagen (self-hosted) ← uses its own generated types
3. schemagen → processes defgen.schema → defgen_types.{h,c}
4. defgen-bootstrap (hand-coded) → processes defgen.def
5. defgen (self-hosted) ← uses defgen_types from step 3
6. Repeat for bddgen, implgen, etc.
```
