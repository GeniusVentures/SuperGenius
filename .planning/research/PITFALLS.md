# Pitfalls Research — GeniusNode Construction Refactor

**Date:** 2026-07-02
**Scope:** Specific failure modes for THIS refactor (variant dispatch, config-file migration, breaking-API cutover with no shim, deployed-config backward compat).

## P1 — Init-order: account created before `node_type` is known  ⭐ HIGHEST RISK

**What goes wrong:** If `New()` still creates the account in the factory (current pattern, `GeniusNode.cpp:139`), the account needs `is_full_node_` — but `node_type_` is only resolved inside `LoadSgnsConfig()` which runs in the constructor. Result: account created with stale/default bool.

**Warning signs:** account behaves as Light even when `node_type: "Full"` is set; UTXO filtering wrong; full-node topic not subscribed.

**Prevention:** Move account creation INTO the constructor, AFTER `LoadSgnsConfig()` (see ARCHITECTURE.md). Add a debug log right after `is_full_node_` is derived AND right after the account is created, confirming both values.

**Phase:** Foundation/reorder phase (step 2-3 in ARCHITECTURE build order).

## P2 — Silent default mismatch when config key is absent

**What goes wrong:** Today `autodht=true`, `base_port=40001`, `is_full_node=false` are the default args. If the new config-read defaults differ even slightly, deployed nodes silently change behavior.

**Warning signs:** node listens on a different port; DHT discovery on/off unexpectedly; connection watermarks shift (400/200 vs 300/150).

**Prevention:** Defaults must be byte-identical to today's: `autodht=true`, `base_port=40001`, `node_type=Light` (→ `is_full_node_=false`). Document each default inline. Test an empty/missing config file explicitly.

**Phase:** Config-read phase + verification phase.

## P3 — Missed call sites during breaking cutover (no shim)

**What goes wrong:** 18 call sites exist (1 `example/node_test/NodeExample.cpp:532`, 17 under `test/src/`). A missed site → link error or — worse — a stale signature that still compiles against a leftover overload.

**Warning signs:** link errors referencing `GeniusNode::NewFromPrivateKey`; a test that "passes" because it's not actually running.

**Prevention:** After deleting old factories, the compiler enforces migration (good — any missed site fails to compile). Add a verification grep step confirming NO `NewFromPrivateKey`/`NewFromMnemonic`/`New(`-with-old-args references remain in `src/`, `example/`, `test/`. The earlier grep found these sites — re-run it.

**Phase:** Call-site migration phase + verification.

## P4 — Test fixtures hardcode the old positional signature

**What goes wrong:** Tests like `test/src/processing_nodes/full_node_test.cpp:49` pass `NewFromPrivateKey(devConfig, privKey.c_str(), false, port, isFullNode)` — positional `(autodht=false, base_port=port, is_full_node=isFullNode)`. The bool `is_full_node` must now become a `sgns_config.json` write; the `port` must become a `network_config.json` write. Easy to drop or mis-write.

**Warning signs:** test passes locally (defaults) but fails in CI with different port collisions; full-node test silently runs as Light.

**Prevention:** Establish ONE helper in the test layer that writes a minimal `network_config.json` (base_port, autodht) and `sgns_config.json` (node_type, is_processor) from a small struct, and reuse it. Many tests already write `sgns_config.json` for `is_processor` (see `test/src/account/account_management_test.cpp:39`) — extend that pattern, don't invent a new one.

**Phase:** Call-site migration phase.

## P5 — `std::variant` visitor not exhaustive

**What goes wrong:** Using a `switch(index())` or a visitor that silently handles only 3 of 4 alternatives — `FromPublicKey` (newly promoted) gets dropped, returns nullptr or throws `std::bad_variant_access`.

**Warning signs:** watch-only node construction crashes or returns null.

**Prevention:** Use `std::visit` with an `if constexpr` chain over `std::is_same_v` (STACK.md) — adding a 5th variant alternative later yields a compile-time reminder. Add a unit test that exercises all 4 alternatives.

**Phase:** Factory-collapse phase.

## P6 — Derived bool drifts from enum

**What goes wrong:** `is_full_node_` is derived once at construction, but if any code path later mutates `node_type_` without re-deriving `is_full_node_` (or vice versa), they desync. Today the bool is the only source of truth.

**Warning signs:** node reports `node_type=Full` but runs with Light connection limits.

**Prevention:** Make `node_type_` the single source of truth; derive `is_full_node_` exactly once immediately after the load and **never** mutate either afterward (they're conceptually `const` post-construction — consider marking them `const` or documenting immutability). No setter.

**Phase:** Config-read phase.

## P7 — Bad config values (typos, wrong type)

**What goes wrong:** `node_type: "ful"` (typo) or `base_port: "40001"` (string instead of uint). Silent fallback to defaults hides the operator's mistake.

**Warning signs:** operator sets `node_type: Archive` but node runs Light; no log explains why.

**Prevention:** Follow existing `HasMember && IsXxx` guards. ADD a WARN log when `node_type` is present but unrecognized, and when `base_port` is present but wrong type. Don't silently swallow.

**Phase:** Config-read phase.

## P8 — `base_port` field type inconsistency with `pubsub_port`

**What goes wrong:** `pubsub_port` is parsed as a string (`GeniusNode.cpp:791`). Copying that style for `base_port` reintroduces the `std::stoi` try/catch fragility.

**Warning signs:** non-numeric `base_port` value throws/caught, node uses default silently.

**Prevention:** Use numeric `IsUint()` for `base_port` (STACK.md). Leave `pubsub_port` alone (out of scope). Comment the intentional divergence.

**Phase:** Config-read phase.

## P9 — Constructor init-list can no longer set `is_full_node_`

**What goes wrong:** `is_full_node_` is currently set in the member-init list (`GeniusNode.cpp:211`). Post-refactor it's derived inside the ctor body from `node_type_`. If left in the init list pointing at a removed param, compile error; if default-init'd and a code path reads it before `LoadSgnsConfig`, undefined behavior.

**Warning signs:** compile error (benign) OR — if zero-init'd — node reads as Light until config loads.

**Prevention:** Remove from init list; default-initialize `is_full_node_ = false` and `node_type_ = NodeType::Light`, then set both inside `LoadSgnsConfig`. Ensure nothing reads either between ctor start and `LoadSgnsConfig`.

**Phase:** Constructor-reorder phase.

## Pitfall → Phase Matrix

| Pitfall | Addressed in phase |
|---------|--------------------|
| P1 init-order | Foundation/reorder |
| P2 default mismatch | Config-read + Verify |
| P3 missed call sites | Migration + Verify (grep) |
| P4 test fixtures | Migration |
| P5 non-exhaustive visitor | Factory-collapse + unit test |
| P6 bool/enum drift | Config-read (immutability) |
| P7 bad config values | Config-read (WARN logs) |
| P8 base_port type | Config-read |
| P9 init-list | Constructor-reorder |
