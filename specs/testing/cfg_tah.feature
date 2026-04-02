Feature: CFG, Topology-Aware Hashing, and Agent IR Analysis
  As an agentic engineer using cosmo-bde
  I want to build and compare control-flow graphs across binary and agentic sources
  So that I can detect structural similarities and normalise agent behaviours

  Background:
    Given the CFG analysis pipeline is initialised
    And the TAH index "test_index" is empty
    And the Agent IR lifter is ready

  # ── CFG Construction: Binary Source ─────────────────────────────────────────

  Scenario: Build CFG from a binary function
    Given a binary function "process_input" at address 0x4010A0 with 8 basic blocks
    When the CFG is constructed with source_type CFG_SOURCE_BINARY
    Then a CfgFunction record should be created with name "process_input"
    And the CfgFunction should have node_count 8
    And the entry node should have node_type CFG_NODE_ENTRY
    And at least one exit node should have node_type CFG_NODE_EXIT

  Scenario: Resolve indirect jump in binary CFG
    Given a binary function "dispatch_handler" with an indirect jump at offset 0x14
    When the CFG is built with indirect-jump resolution enabled
    Then the indirect-jump node should have node_type CFG_NODE_INDIRECT
    And the resolved targets should produce CFG_EDGE_INDIRECT edges
    # has_indirect tracks UNRESOLVED indirect edges (per cfg.schema).
    # Successful resolution leaves zero unresolved indirect edges.
    And has_indirect on the CfgFunction should be 0

  Scenario: Detect loop in binary CFG
    Given a binary function "scan_buffer" with a back-edge from block 5 to block 2
    When the CFG is constructed
    Then the loop-head node should have node_type CFG_NODE_LOOP_HEAD
    And has_recursion on the CfgFunction should be 1
    And cyclomatic_complexity of the analysis result should be at least 2

  # ── CFG Construction: Agentic Sources ───────────────────────────────────────

  Scenario: Build CFG from an agent workflow spec
    Given an agent workflow spec "agent_graph.hsm" with 9 states
    When the CFG is constructed with source_type CFG_SOURCE_AGENTIC
    Then a CfgFunction record should be created with name "AgentGraph"
    And node_count should equal the number of states in the HSM
    And the entry node label should be "Idle"
    And the exit node labels should include "Done"

  Scenario: Build CFG from a human workflow
    Given a human decision process "onboarding_flow" with 5 decision points
    When the CFG is constructed with source_type CFG_SOURCE_HUMAN
    Then each decision point should produce a CFG_NODE_CONDITION node
    And each condition node should have exactly two outgoing edges
    And edges should carry guard conditions describing each branch

  Scenario: Build CFG from an LLM chain-of-thought
    Given an LLM reasoning trace with 3 tool calls and 2 conditional branches
    When the CFG is constructed with source_type CFG_SOURCE_LLM
    Then tool-call nodes should have node_type CFG_NODE_CALL
    And call edges should have edge_type CFG_EDGE_CALL
    And return edges should have edge_type CFG_EDGE_RETURN

  # ── Topology-Aware Hashing ───────────────────────────────────────────────────

  Scenario: Compute TAH signature for a binary function
    Given a CfgFunction "process_input" with source_type CFG_SOURCE_BINARY
    When the TAH signature is computed
    Then a TahSignature record should be created with function_name "process_input"
    And dim should be greater than 0
    And node_count and edge_count should match the CfgFunction

  Scenario: Identical topology produces the same signature
    Given two CfgFunctions "func_a" and "func_b" with identical graph topology
    When TAH signatures are computed for both
    Then the similarity score between them should be 100
    And the match_type should be TAH_MATCH_EXACT

  Scenario: Structurally similar CFGs produce high similarity score
    Given "agent_workflow_v1" and "agent_workflow_v2" with one extra node in v2
    When TAH signatures are computed and compared
    Then the similarity_score should be at least TAH_THRESHOLD_SIMILAR
    And the match_type should be TAH_MATCH_SIMILAR
    And node_count_delta should equal 1

  Scenario: Structurally different CFGs produce low similarity score
    Given "linear_function" with 3 nodes and "complex_function" with 15 nodes and loops
    When TAH signatures are compared
    Then the similarity_score should be below TAH_THRESHOLD_PARTIAL
    And the match_type should be TAH_MATCH_DIFFERENT

  Scenario: Cross-source TAH comparison (binary vs agentic)
    Given a binary function "state_dispatch" and an agent HSM "AgentGraph"
    Both with similar branching structure
    When TAH signatures from both sources are compared
    Then the similarity_score should reflect their structural likeness
    And sig_a_source and sig_b_source should differ
    And the comparison should still complete without error

  # ── TAH Index Search ─────────────────────────────────────────────────────────

  Scenario: Index multiple signatures and search for nearest match
    Given the TAH index "test_index" contains signatures for 10 known agents
    And the query signature "unknown_agent" has high structural similarity to "agent_3"
    When a TAH search is performed with threshold TAH_THRESHOLD_SIMILAR
    Then the top result should have match_name "agent_3"
    And rank 1 should have the highest similarity_score
    And the search should complete without full graph-isomorphism checks

  # ── Agent IR Lifting ─────────────────────────────────────────────────────────

  Scenario: Lift LangChain agent to Agent IR
    Given a LangChain agent with a "search_web" tool call and conditional routing
    When an AgentIrLiftRequest is submitted with framework AIR_FW_LANGCHAIN
    Then the lift result should have status AIR_STATUS_OK
    And the tool call should produce an AgentIrOp with opcode AIR_OP_CALL
    And the conditional routing should produce an AgentIrOp with opcode AIR_OP_BRANCH

  Scenario: Map VEX IR tags to Agent IR opcodes
    Given the VexIrMapping table is loaded
    When I look up the VEX tag "Ist_WrTmp"
    Then the agent_opcode should be AIR_OP_ASSIGN
    When I look up the VEX tag "Ist_Exit"
    Then the agent_opcode should be AIR_OP_BRANCH
    When I look up the VEX tag "Ist_Dirty"
    Then the agent_opcode should be AIR_OP_EFFECT

  Scenario: Lift an agent and build its CFG in one pass
    Given an AgentIrLiftRequest with lift_cfg set to 1
    And lift_tah set to 1
    When the lift is performed
    Then an AgentIrFunction should be returned
    And a CfgFunction should be available for the lifted agent
    And a TahSignature should be computed from the CFG
