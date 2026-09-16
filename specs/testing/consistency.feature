Feature: Code-Generation Consistency Enforcement
  As an agentic engineer or automated pipeline in cosmo-bde
  I want every generated source file — regardless of whether it was produced by
  a deterministic Ring 0 tool, an LLM, or both in tandem — to conform to the
  structural contracts defined by its spec file
  So that the codebase stays coherent and every generation path is interchangeable

  Background:
    Given the ConsistencyEnforcement pipeline is initialised
    And the spec file "specs/domain/example.schema" has a registered ConsistencyContract
    And the reference TAH signature was computed from the deterministic schemagen output
    And the default policy_mode is CONSISTENCY_POLICY_ENFORCE

  # ── Deterministic Code-Gen Path ──────────────────────────────────────────────

  Scenario: Deterministic generator output passes structural check
    Given schemagen runs on "specs/domain/example.schema"
    And the output is "gen/domain/example_types.h"
    When the ConsistencyReport is generated
    Then the report status should be REPORT_STATUS_PASSED
    And structural_score should be 100
    And violation_count should be 0

  Scenario: Deterministic output flags a naming violation
    Given schemagen generates a file with a type named "example" instead of "Example_t"
    When the ConsistencyReport is generated
    Then violation_count should be at least 1
    And at least one violation should have violation_type VIOLATION_NAMING
    And the fix_hint should mention the expected naming pattern

  Scenario: Deterministic output missing a schema type fails coverage
    Given schemagen omits the "Example" type from its generated header
    When the ConsistencyReport is generated
    Then at least one violation should have violation_type VIOLATION_MISSING_TYPE
    And type_coverage should be below the contract's type_coverage_min
    And the report status should be REPORT_STATUS_FAILED

  # ── LLM Code-Gen Path ────────────────────────────────────────────────────────

  Scenario: LLM output with correct structure passes enforcement
    Given an LLM generates "gen/domain/example_types.h" from "specs/domain/example.schema"
    And the LLM output has a TAH similarity score of 95 against the reference
    When the ConsistencyReport is generated for generator_type CODEGEN_TYPE_LLM
    Then structural_score should be 95
    And match_type should be TAH_MATCH_SIMILAR
    And the report status should be REPORT_STATUS_PASSED

  Scenario: LLM output with structural drift below threshold is rejected
    Given an LLM generates code with a completely different control-flow structure
    And the TAH similarity score is 30 against the reference
    When the ConsistencyReport is generated for generator_type CODEGEN_TYPE_LLM
    Then structural_score should be 30
    And at least one violation should have violation_type VIOLATION_STRUCTURAL
    And the report status should be REPORT_STATUS_FAILED
    And the violation message should contain the expected similarity threshold

  Scenario: LLM output missing schema fields is flagged
    Given an LLM generates code that omits fields "value" and "enabled" from the Example type
    When the ConsistencyReport is generated
    Then violations should include VIOLATION_MISSING_FIELD for "value"
    And violations should include VIOLATION_MISSING_FIELD for "enabled"
    And field_coverage should be 50

  Scenario: LLM output with a forbidden pattern is blocked
    Given an LLM introduces a global mutable state not present in the spec
    When the ConsistencyReport is generated with policy CONSISTENCY_POLICY_STRICT
    Then at least one violation should have violation_type VIOLATION_FORBIDDEN
    And violation severity should be VIOLATION_SEVERITY_FATAL
    And the report status should be REPORT_STATUS_BLOCKED

  # ── Tandem Mode (Deterministic + LLM) ────────────────────────────────────────

  Scenario: Tandem outputs agree — both accepted
    Given schemagen produces reference output with TAH signature S1
    And an LLM produces output with TAH signature S2
    And the similarity between S1 and S2 is 92
    When the TandemDiff is computed
    Then consensus_status should be TANDEM_CONSENSUS_AGREE
    And drift_score in the ConsistencyReport should be 8
    And both runs should have report status REPORT_STATUS_PASSED

  Scenario: Tandem outputs diverge — LLM output blocked, deterministic accepted
    Given schemagen produces reference output with TAH signature S1
    And an LLM produces output with TAH signature S2
    And the similarity between S1 and S2 is 45
    When the TandemDiff is computed
    Then consensus_status should be TANDEM_CONSENSUS_DIVERGED
    And the TandemDiff recommendation should suggest reconciling the LLM output
    And the LLM CodegenRun ConsistencyReport status should be REPORT_STATUS_FAILED
    And the deterministic CodegenRun ConsistencyReport status should be REPORT_STATUS_PASSED

  Scenario: Tandem cross-validation upgrades a borderline LLM output
    Given the LLM output has TAH similarity 68 (just below TAH_THRESHOLD_SIMILAR)
    And the tandem deterministic output has similarity 100
    And the tandem diff shows only naming differences
    When the ConsistencyReport is generated in tandem mode
    Then consensus_status should be TANDEM_CONSENSUS_MINOR_DIFF
    And naming_delta should be greater than 0
    And the recommendation should hint at applying the deterministic naming

  # ── FSM / Behaviour Spec Consistency ─────────────────────────────────────────

  Scenario: LLM-generated agent code covers all HSM states
    Given the spec "specs/behavior/agent_graph.hsm" has 9 states
    And an LLM generates an agent implementation
    When the ConsistencyReport is generated
    Then state_coverage should be 100
    And transition_coverage should be 100

  Scenario: LLM skips an error handling state — violation raised
    Given the spec "specs/behavior/codegen_consistency.hsm" includes a "Blocked" state
    And an LLM generates enforcement code omitting the "Blocked" state
    When the ConsistencyReport is generated
    Then violations should include VIOLATION_MISSING_STATE for "Blocked"
    And the fix_hint should explain that "Blocked" is a required exit node

  # ── Policy Modes ─────────────────────────────────────────────────────────────

  Scenario: Report-only policy never blocks generation
    Given a ConsistencyContract with policy_mode CONSISTENCY_POLICY_REPORT_ONLY
    And the LLM output has structural_score 25
    When the ConsistencyReport is generated
    Then the pipeline exit code should be 0
    And violation_count should be greater than 0
    And the report should be written to the output log

  Scenario: Warn policy emits warnings but returns success
    Given a ConsistencyContract with policy_mode CONSISTENCY_POLICY_WARN
    And the LLM output has structural_score 65
    When the ConsistencyReport is generated
    Then the pipeline exit code should be 0
    And the report status should be REPORT_STATUS_WARNED
    And warning messages should be printed

  Scenario: Enforce policy blocks on error-severity violations
    Given a ConsistencyContract with policy_mode CONSISTENCY_POLICY_ENFORCE
    And the generated output has two error-severity violations
    When the ConsistencyReport is generated
    Then the report status should be REPORT_STATUS_FAILED
    And the pipeline should exit with non-zero exit code

  Scenario: Strict policy stops on first fatal violation
    Given a ConsistencyContract with policy_mode CONSISTENCY_POLICY_STRICT
    And the generated output has a forbidden pattern
    When the ConsistencyReport is generated
    Then the report status should be REPORT_STATUS_BLOCKED
    And subsequent checks should not run
    And the pipeline should exit immediately

  # ── Human-Written Code ───────────────────────────────────────────────────────

  Scenario: Hand-written code passes when it matches spec structure
    Given a developer writes "src/example.c" by hand following the spec
    And the file is submitted as generator_type CODEGEN_TYPE_HUMAN
    When the ConsistencyReport is generated
    Then structural_score should be at least ref_similarity_min from the contract
    And the report should include the generator_name "human"

  Scenario: Human-written code that ignores the spec is flagged
    Given a developer hand-writes code that diverges significantly from the spec
    When the ConsistencyReport is generated
    Then violation_count should be greater than 0
    And the fix_hint fields should reference the authoritative spec path
