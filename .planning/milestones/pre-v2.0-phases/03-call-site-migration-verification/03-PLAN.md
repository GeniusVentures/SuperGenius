---
id: 03
title: "Delete old factories + old private ctor (INTF-04); final verification grep + green build/suite"
phase: 3
wave: 3
depends_on: ["02"]
requirements: [INTF-04, MIG-03, MIG-04]
files_modified:
  - src/account/GeniusNode.hpp
  - src/account/GeniusNode.cpp
autonomous: true
---

# Plan 03: Delete Old Factories + Final Verification

<objective>
Delete the 3 old `GeniusNode` factories (`New(autodht, port_seed, is_full_node)`,
`NewFromPrivateKey`, `NewFromMnemonic`) and the old private ctor
`(dev_config, account, autodht, port_seed, is_full_node)` — INTF-04 — as the **final** step,
after Plan 02 migrated every call site. Then verify MIG-03 (full build + CTest green) and MIG-04
(grep confirms no stale old-factory references anywhere). This closes the milestone.

Per CONTEXT D-03: this plan runs only after Plan 02 leaves zero callers of the old factories,
so the build stays green throughout. The canonical `New(dev_config, AccountSource)` (Phase 2) is
the sole entry point afterward.
</objective>

<implementation_notes>
- The old private ctor is called ONLY by the 3 old factories (`new GeniusNode(dev_config, account, autodht, port_seed, is_full_node)`). Once those are deleted, the old ctor has no callers → delete it too. The new ctor `(dev_config, AccountSource)` (Phase 2) stays.
- `NewFromMnemonic` has 0 call sites in `example/`/`test/` — deleting it is pure dead-code removal.
- The `GeniusAccount` factories (`GeniusAccount::NewFromPrivateKey` etc.) are NOT touched — the Phase-2 `std::visit` visitor calls them; they stay.
- Do NOT delete the canonical `New(dev_config, AccountSource)` (2-arg, Phase 2) or the new private ctor `(dev_config, AccountSource)` — only the OLD 4-arg factory + `NewFromPrivateKey` + `NewFromMnemonic` + the old 5-arg private ctor.
</implementation_notes>

---

## Task 1: Delete the 3 old factories + old private ctor (declarations + definitions)

<task id="1">
<read_first>
- src/account/GeniusNode.hpp (old factory declarations: `New(dev_config, autodht, port_seed, is_full_node)` ~line 82-85, `NewFromPrivateKey` ~98-102, `NewFromMnemonic` ~115-119; old private ctor `(dev_config, account, autodht, port_seed, is_full_node)` ~700-704; KEEP the Phase-2 canonical `New(dev_config, AccountSource)` and the new private ctor `(dev_config, AccountSource)`)
- src/account/GeniusNode.cpp (old factory definitions: `GeniusNode::New(autodht, port_seed, is_full_node)` ~line 165 after Phase-2 additions, `NewFromPrivateKey`, `NewFromMnemonic`; old private ctor `GeniusNode::GeniusNode(dev_config, account, autodht, port_seed, is_full_node)`; KEEP the canonical `New(dev_config, AccountSource)` and the new `(dev_config, AccountSource)` ctor)
- .planning/phases/03-call-site-migration-verification/02-PLAN.md (confirms all call sites migrated — no callers remain)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md (D-03 ordering)
</read_first>

<action>
Delete (header + implementation, matching pairs):

1. **`GeniusNode.hpp`** — remove the Doxygen + declaration for:
   - `static std::shared_ptr<GeniusNode> New( const DevConfig &dev_config, bool autodht = true, uint16_t port_seed = 40001, bool is_full_node = false );` (the OLD 4-arg overload — KEEP the Phase-2 2-arg `New(dev_config, AccountSource)`)
   - `static std::shared_ptr<GeniusNode> NewFromPrivateKey( const DevConfig &dev_config, const char *eth_private_key, bool autodht = true, uint16_t port_seed = 40001, bool is_full_node = false );`
   - `static std::shared_ptr<GeniusNode> NewFromMnemonic( const DevConfig &dev_config, const std::string &mnemonic, bool autodht = true, uint16_t port_seed = 40001, bool is_full_node = false );`
   - The OLD private ctor `GeniusNode( const DevConfig &dev_config, std::shared_ptr<GeniusAccount> account, bool autodht, uint16_t port_seed, bool is_full_node );` (KEEP the Phase-2 `GeniusNode(dev_config, AccountSource)` ctor)

2. **`GeniusNode.cpp`** — remove the matching definitions:
   - `std::shared_ptr<GeniusNode> GeniusNode::New( const DevConfig &dev_config, bool autodht, uint16_t port_seed, bool is_full_node )` { ... } (old 4-arg factory body)
   - `std::shared_ptr<GeniusNode> GeniusNode::NewFromPrivateKey(...)` { ... }
   - `std::shared_ptr<GeniusNode> GeniusNode::NewFromMnemonic(...)` { ... }
   - `GeniusNode::GeniusNode( const DevConfig &dev_config, std::shared_ptr<GeniusAccount> account, bool autodht, uint16_t port_seed, bool is_full_node )` { ... } (old private ctor body)

   Leave the canonical `New(dev_config, AccountSource)`, the new `(dev_config, AccountSource)` ctor, the `WriteNetworkConfig`/`WriteSgnsConfig` helpers, and all Phase-1/2 work untouched.
</action>

<acceptance_criteria>
- `grep -rn "NewFromPrivateKey\|NewFromMnemonic" src/account/GeniusNode.hpp src/account/GeniusNode.cpp` returns 0 (the old factories gone; `GeniusAccount::NewFromPrivateKey` in GeniusAccount.{hpp,cpp} is unaffected)
- `grep -c "GeniusNode::New( const DevConfig &dev_config,$" src/account/GeniusNode.cpp` reflects only the canonical 2-arg factory (the old 4-arg body is gone)
- `grep -c "GeniusNode::GeniusNode( const DevConfig &dev_config, AccountSource source )" src/account/GeniusNode.cpp` returns 1 (the new ctor survives)
- The old private ctor `(dev_config, std::shared_ptr<GeniusAccount> account, ...)` declaration + definition are gone
- `genius_node` target compiles (no dangling references — Plan 02 removed all callers)
</acceptance_criteria>
</task>

---

## Task 2: Final verification — MIG-04 grep + MIG-03 build/suite green

<task id="2" depends_on="1">
<read_first>
- .planning/REQUIREMENTS.md (MIG-03, MIG-04 acceptance)
- .planning/ROADMAP.md (Phase 3 success criteria #3, #4)
</read_first>

<action>
Run the milestone's closing verification:

1. **MIG-04 stale-reference grep** — confirm no old-factory references remain anywhere in `src/`, `example/`, `test/`:
   ```bash
   grep -rn "GeniusNode::NewFromPrivateKey\|GeniusNode::NewFromMnemonic\|GeniusNode::New(" src/ example/ test/ \
     | grep -v "GeniusNode::New( const DevConfig &dev_config, AccountSource" \
     | grep -v "GeniusAccount::NewFromPrivateKey"
   ```
   (Excludes the canonical `New(dev_config, AccountSource)` declaration/definition and the unrelated `GeniusAccount::NewFromPrivateKey`.) Expected: **zero output**. Any hit is a missed migration or stale reference → fix before proceeding.

2. **MIG-03 build + suite** — full build passes; the existing CTest suite is green (no behavior regression). The milestone's full test set must pass: `account_management_test`, `network_config_precedence_test`, `node_type_derivation_test`, `transaction_sync_test`, `transaction_crash_test`, `migration_sync_test`, `processing_multi_test`, `processing_nodes_test`, `full_node_test`, `child_tokens_test`, `multi_account_sync`, `blockchain_genesis_test`, `node_initialization_progress`.
</action>

<acceptance_criteria>
- The MIG-04 grep returns **zero** non-excluded matches across `src/`, `example/`, `test/`
- `cmake --build <build-dir>` exits 0 (full project)
- `ctest --test-dir <build-dir> --output-on-failure` exits 0 (full suite green — no behavior change)
- Specifically the 13 migrated test targets + the Phase-1/2 tests all pass
</acceptance_criteria>
</task>

---

<threat_model>
**ASVS Level:** 1 (dead-code removal — reduces attack surface).

**Threats & mitigations:**
- **T1 — A lingering caller of a deleted factory slips through** (e.g., a call site Plan 02 missed, or an internal `src/` caller): the build would fail with an undefined-reference / undeclared-identifier error. **Mitigation:** the build (Task 2) is the gate — a missed caller cannot compile. The MIG-04 grep is a second net. Severity: LOW (compile-time catch).
- **T2 — `NewFromMnemonic` dead-code removal hides a future caller:** it has 0 call sites today; removing it is safe. If a future phase needs mnemonic-restore, it uses `New(dev_config, FromMnemonic{...})` (the variant already supports it). Severity: INFORMATIONAL.

**Block-on threshold (high):** No HIGH-severity threats. Proceed.
</threat_model>

---

<must_haves>
<truths>
- The 3 old factories (`New(autodht, port_seed, is_full_node)`, `NewFromPrivateKey`, `NewFromMnemonic`) are deleted from `GeniusNode.{hpp,cpp}`
- The old private ctor `(dev_config, account, autodht, port_seed, is_full_node)` is deleted (no callers after factory deletion)
- The canonical `New(dev_config, AccountSource)` (Phase 2) is the sole public factory; the new `(dev_config, AccountSource)` ctor is the sole private ctor
- MIG-04: no `NewFromPrivateKey` / `NewFromMnemonic` / old-`New(4-arg)` references remain in `src/`, `example/`, `test/`
- MIG-03: full build + CTest suite green, no behavior change
</truths>

<verification>
1. **Deletion audit:** `grep -rn "NewFromPrivateKey\|NewFromMnemonic" src/account/GeniusNode.{hpp,cpp}` = 0; old 4-arg `New` + old 5-arg ctor gone.
2. **Retention audit:** canonical `New(dev_config, AccountSource)` + new ctor + `WriteNetworkConfig`/`WriteSgnsConfig` + all Phase-1/2 work intact.
3. **MIG-04 grep:** zero non-excluded stale references across `src/`, `example/`, `test/`.
4. **MIG-03 build + CTest:** full build + suite green.
</verification>

<goal_alignment>
INTF-04 (delete old factories — moved to Phase 3 per 02-CONTEXT D-01) + MIG-03 (build + suite green) + MIG-04 (no stale references). This plan closes the milestone: `New(dev_config, AccountSource)` is the sole, self-documenting, config-driven factory. Run `/gsd-complete-milestone` after this plan's verification passes.
</goal_alignment>
