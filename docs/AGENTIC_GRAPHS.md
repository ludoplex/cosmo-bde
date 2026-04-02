# Agentic Graphs: TAH, CFG, VEX IR, and Consistency Enforcement in cosmo-bde

> **Scope:** This document explains how topology-aware hashing (TAH),
> control-flow graphs (CFG), VEX IR lifting, and transition graphs apply
> to both classical binary analysis **and** human, LLM-agent, and agentic
> engineering via cosmo-bde.  It also describes the consistency enforcement
> layer that ensures every code-generation path — deterministic, LLM, or
> both in tandem — produces coherent, spec-compliant output.

---

## The Core Idea

In binary analysis, angr builds a **control-flow graph (CFG)** per function by:

1. Lifting x86/ARM machine code to VEX IR (a normalised intermediate form).
2. Resolving indirect jumps to fill in missing CFG edges.
3. Storing the per-function graph as `function.transition_graph`.
4. Hashing graph topology for similarity search via **topology-aware hashing (TAH)**.

cosmo-bde borrows this entire pipeline and applies it to **agentic engineering**:

| Binary Analysis (angr) | Agentic Engineering (cosmo-bde) |
|------------------------|----------------------------------|
| Machine-code function  | Agent / workflow / human process |
| VEX IR                 | Agent IR (`agent_ir.schema`)     |
| `transition_graph`     | `CfgFunction` + `CfgGraph`       |
| TAH signature          | `TahSignature`                   |
| Malware clone search   | Agent-pattern deduplication      |
| Indirect jump resolution | Dynamic tool-dispatch resolution |

---

## New Spec Files

### `specs/domain/cfg.schema`

Defines the CFG types used across all source domains.

| Type | Role |
|------|------|
| `CfgNode` | One basic block (binary) or step (agent/human/LLM) |
| `CfgEdge` | Directed edge with `edge_type` (direct, conditional, call, …) |
| `CfgFunction` | Per-function or per-agent transition graph |
| `CfgGraph` | Full inter-function / multi-agent call graph |
| `CfgAnalysisResult` | Cyclomatic complexity, unresolved edges, errors |

`source_type` on every node and function encodes the origin:

```
CFG_SOURCE_BINARY  = 0   lifted from machine code
CFG_SOURCE_HUMAN   = 1   human workflow / decision tree
CFG_SOURCE_LLM     = 2   LLM chain-of-thought / tool-use graph
CFG_SOURCE_AGENTIC = 3   multi-agent orchestration
```

### `specs/domain/tah.schema`

Defines topology-aware hashing for CFG similarity at scale.

| Type | Role |
|------|------|
| `TahSignature` | Fixed-length structural fingerprint (16 × u64 hash vector) |
| `TahComparison` | Similarity score + match_type between two signatures |
| `TahIndex` | Searchable corpus of signatures (binary, agent, or mixed) |
| `TahSearchResult` | Ranked nearest-neighbour result |

**Why this beats graph isomorphism:** Full graph isomorphism is NP-hard.
TAH maps topology to a high-dimensional vector so that similar structures
produce nearby vectors — enabling O(1) approximate nearest-neighbour lookup.

Applications:
- Malware clone detection against known-bad binaries.
- Detecting duplicate agent patterns across LLM-generated workflows.
- Versioning state-machine topologies with drift detection.
- Finding the "same algorithm in disguise" across frameworks (LangChain,
  CrewAI, AutoGen, cosmo-bde).

### `specs/domain/agent_ir.schema`

Defines **Agent IR** — the agentic analogue of VEX IR.

VEX IR normalises CPU-architecture diversity (x86, ARM, MIPS…) into one
analysis-friendly form.  Agent IR normalises framework diversity
(LangChain, CrewAI, AutoGen, raw Python, cosmo-bde specs…) into one form.

#### VEX → Agent IR Mapping

| VEX IR Tag | Agent IR Opcode | Binary Example | Agentic Example |
|------------|-----------------|----------------|-----------------|
| `Ist_IMark` | `AIR_OP_STEP` | Instruction address marker | One atomic agent action |
| `Ist_WrTmp` | `AIR_OP_ASSIGN` | Bind result to temp var | Bind tool result to name |
| `Ist_Get` | `AIR_OP_READ` | Read guest register | Read agent context/memory |
| `Ist_Put` | `AIR_OP_WRITE` | Write guest register | Update context/memory |
| `Ist_CCall` | `AIR_OP_CALL` | Pure C helper call | Invoke tool or sub-agent |
| `Ist_Exit` | `AIR_OP_BRANCH` | Conditional jump | Conditional state transition |
| `Ist_Jump` | `AIR_OP_JUMP` | Unconditional jump | Unconditional transition |
| `Ist_Dirty` | `AIR_OP_EFFECT` | Side-effecting helper | Side-effecting operation |

### `specs/behavior/agent_graph.hsm`

The agent lifecycle modelled as a hierarchical state machine whose state
topology is annotated with CFG node-type semantics:

```
Idle          → CFG_NODE_ENTRY
Working       → composite (function body)
  Thinking    → CFG_NODE_CONDITION   (LLM reasons, branches on next action)
  Calling     → CFG_NODE_CALL        (tool/sub-agent invocation)
  Observing   → CFG_NODE_NORMAL      (environment read)
Responding    → CFG_NODE_EXIT        (emit answer)
Error         → composite error subgraph
  Recoverable → CFG_NODE_LOOP_HEAD   (retry back-edge)
  Fatal       → CFG_NODE_EXIT (error)
Done          → CFG_NODE_EXIT
```

`hsmgen` processes this file and generates a zero-dependency C FSM.

---

## Consistency Enforcement

### `specs/domain/consistency.schema`

Defines the contract types that enforce structural and semantic coherence
across **all** code-generation paths.

| Type | Role |
|------|------|
| `ConsistencyContract` | Binding of a spec file to a reference TAH signature + policy |
| `CodegenRun` | Metadata for one generation pass (tool, timestamp, generator type) |
| `ConsistencyViolation` | One detected deviation from the contract |
| `ConsistencyReport` | Aggregated pass/fail summary with scores and counts |
| `TandemDiff` | Cross-validation result when deterministic and LLM outputs coexist |
| `EnforcementConfig` | Project-level policy overrides |

### `specs/behavior/codegen_consistency.hsm`

The enforcement pipeline itself is a state machine generated by `hsmgen`:

```
Idle → Loading (LoadingContract → LoadingCandidate)
     → Comparing (StructuralCheck → NamingCheck → CoverageCheck → TandemCheck)
     → RecordViolation (Structural | Naming | Coverage | Drift)
     → AllChecksDone
     → Reporting
     → Enforcing (Deciding → Done | Blocked)
```

Because this machine is also subject to consistency enforcement, its own
TAH signature becomes the reference for any tool or LLM that implements
an equivalent enforcement workflow — dogfooding in action.

### Four Policy Modes

| Mode | Effect | Use case |
|------|--------|----------|
| `REPORT_ONLY` | Log violations, always exit 0 | Exploratory / audit |
| `WARN` | Print warnings, exit 0 | Team awareness |
| `ENFORCE` | Non-zero exit on error-severity violations | CI gate |
| `STRICT` | Stop on first fatal violation | Safety-critical / production |

### Three Generation Paths — One Enforcement Gate

```
┌─────────────────────────────────────────────────────────────────┐
│                     SPEC (Source of Truth)                      │
│         specs/domain/*.schema  specs/behavior/*.hsm  …          │
└──────────────┬────────────────────────────────┬─────────────────┘
               │                                │
       deterministic                          LLM
       Ring 0 tools                     (any model)
               │                                │
               ▼                                ▼
   gen/domain/*_types.c,h          llm_gen/*_types.c,h
               │                                │
               └──────────────┬─────────────────┘
                              │
                    ┌─────────▼─────────┐
                    │  CONSISTENCY GATE  │
                    │  (TAH + naming +   │
                    │   coverage checks) │
                    └─────────┬─────────┘
                      pass ◄──┴──► fail
                       │               │
                  accepted         blocked / warned
```

**Tandem mode** runs both paths and cross-validates using `TandemDiff`.
A high `drift_score` indicates the LLM and deterministic outputs have
diverged structurally; the deterministic output is always taken as ground
truth for resolving ties.

---

## Workflow

```bash
# 1. Edit a spec
vim specs/domain/my_domain.schema

# 2. Run deterministic generator (produces reference TAH)
make regen

# 3. Register contract (records reference TahSignature + policy)
./scripts/consistency_register.sh specs/domain/my_domain.schema

# 4a. Deterministic-only enforcement (always passes by construction)
make verify          # existing regen-and-diff gate

# 4b. Check LLM-generated code
./scripts/consistency_check.sh llm_gen/my_domain_types.c \
    --spec specs/domain/my_domain.schema \
    --policy enforce

# 4c. Tandem: cross-validate det vs LLM
./scripts/consistency_check.sh llm_gen/my_domain_types.c \
    --tandem gen/domain/my_domain_types.c \
    --spec specs/domain/my_domain.schema \
    --policy enforce
```

---

## BDD Test Coverage

`specs/testing/cfg_tah.feature` covers:
- CFG construction from binary, agent, human, and LLM sources.
- TAH signature computation and cross-source comparison.
- VEX IR → Agent IR opcode mapping.
- Combined lift-and-hash in a single request.

`specs/testing/consistency.feature` covers:
- Deterministic, LLM, tandem, and human generation paths.
- All four policy modes (report-only, warn, enforce, strict).
- Every violation type: structural, naming, missing type/field/state, drift, forbidden.

---

## Ring Classification

| Artifact | Ring | Rationale |
|----------|------|-----------|
| `cfg.schema`, `tah.schema`, `agent_ir.schema`, `consistency.schema` | 0 | Processed by `schemagen` (pure C, no deps) |
| `agent_graph.hsm`, `codegen_consistency.hsm` | 0 | Processed by `hsmgen` (pure C) |
| `cfg_tah.feature`, `consistency.feature` | 0 | Processed by `bddgen` (pure C) |
| angr, TAH papers | External reference | Not a build dependency |

---

## See Also

- [`specs/domain/cfg.schema`](../specs/domain/cfg.schema) — CFG types
- [`specs/domain/tah.schema`](../specs/domain/tah.schema) — TAH types
- [`specs/domain/agent_ir.schema`](../specs/domain/agent_ir.schema) — Agent IR
- [`specs/domain/consistency.schema`](../specs/domain/consistency.schema) — Enforcement contracts
- [`specs/behavior/agent_graph.hsm`](../specs/behavior/agent_graph.hsm) — Agent lifecycle FSM
- [`specs/behavior/codegen_consistency.hsm`](../specs/behavior/codegen_consistency.hsm) — Enforcement pipeline FSM
- [`specs/testing/cfg_tah.feature`](../specs/testing/cfg_tah.feature) — CFG/TAH BDD tests
- [`specs/testing/consistency.feature`](../specs/testing/consistency.feature) — Consistency BDD tests
- [`VENDORS.md`](../VENDORS.md) — angr and TAH external references
- [`RING_CLASSIFICATION.md`](../RING_CLASSIFICATION.md) — Ring definitions
