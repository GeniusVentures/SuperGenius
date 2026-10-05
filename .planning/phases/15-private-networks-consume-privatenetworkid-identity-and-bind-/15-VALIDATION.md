---
phase: 15
slug: private-networks-consume-privatenetworkid-identity-and-bind
status: ready
nyquist_compliant: true
wave_0_complete: true
created: 2026-08-31
---

# Phase 15 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest over existing `test/src/*` suites (GoogleTest-style binaries, per existing `pubsub_counts`) |
| **Config file** | root `CMakeLists.txt` + `test/CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build/OSX/Release -R <suite> --output-on-failure` |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | ~300 seconds |

> **Wave 0 precondition:** existing `build/OSX/{Release,Debug}` dirs are stale (configured against the old keyless third-party tree). Reconfigure against `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release` before any test run (research finding — environment blocker).

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build/OSX/Release -R <suite> --output-on-failure`
- **After every plan wave:** Run `ctest --test-dir build/OSX/Release --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 300 seconds

---

## Per-Task Verification Map

> Reconciled by planner 2026-08-31, revised 2026-09-01 after checker feedback (8 plans, 5 waves — Waves 3-5 serialized by exclusive `src/account/GeniusNode.cpp` ownership, now encoded in `depends_on`). Requirement IDs are derived (D-01..D-11 / PNET-*) because ROADMAP lists `Requirements: TBD`.
>
> **Reconciled post-execution 2026-10-05 (validation audit):** statuses flipped to green against a confirmation battery run the same day — 15 suites, 15/15 passed, 0 failed (348.37s), binaries rebuilt from the post-hygiene-fix source state (85375ef1f/05a2c00f5):
> `ctest --test-dir build/OSX/Release -R "network_config_private_network_test|peer_registry_test|network_registry_test|network_membership_filter_test|validator_registry_scope_test|processing_core_gating_test|task_keys_scope_test|pubsub_counts_test|private_network_registry_binding_test|processing_subtask_queue_channel_pubsub_test|processing_service_test|task_queue_test|transaction_manager_pending_lifecycle_test|securecrdt_interface_test|trustedpeerregistry_genesis_test" --output-on-failure --timeout 300`
> Plan 15-04 was descoped by owner order (see rows 15-04-01/02); its D-07 obligation was delivered at the application layer by gap-closure plans 15-11..15-15. Plans 09-18 (post-verification gap-closure, "GC" wave) were appended to this map by the same audit; per-plan detail lives in each `15-NN-SUMMARY.md`.

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 15-01-01 | 01 | 1 | D-10 (env) | T-15-SC | build bound to dev_pnets install; existing pnet handshake test green | integration | `ctest --test-dir build/OSX/Release -R pubsub_counts --output-on-failure` | ✅ (extend run) | ✅ green (battery 2026-10-05) |
| 15-01-02 | 01 | 1 | D-10 | — | owner confirms implementation base + identity encoding before Wave 2 | checkpoint | blocking decision (no automated) | — | ✅ resolved — decision made during execution (15-01 Task 2, recorded verbatim in 15-01-SUMMARY key-decisions: base = merge-closeout-first @ dc7b40f1; encoding = 0x-hex-32B, `^0x[0-9a-fA-F]{64}$`) |
| 15-01-03 | 01 | 1 | D-01, D-02 / PNET-CFG | T-15-01, T-15-02 | malformed/all-zero `private_network_id` rejected at load; half-provisioned pair (id xor network_key) rejected at load; absent = public; public id only in logs | unit | `ctest --test-dir build/OSX/Release -R network_config_private_network --output-on-failure` | ✅ `test/src/account/network_config_private_network_test.cpp` | ✅ green (battery 2026-10-05) |
| 15-02-01 | 02 | 1 | D-04 / PNET-REG | T-15-04 | PeerRegistry interface compiles; cached-only resolution documented | build | `cmake --build build/OSX/Release --target securecrdt trustedpeer` (tree is Unix-Makefiles, not Ninja) | ✅ `src/peerregistry/PeerRegistry.hpp` | ✅ green — battery binaries compiled from current source; interface exercised by peer_registry_test (green) |
| 15-02-02 | 02 | 1 | D-04 / PNET-REG | T-15-05 | SecureCrdtRegistryEntry carries explicit registry authority; regex contract byte-stable | unit | `ctest -R securecrdt` (regression) | ✅ | ✅ green (securecrdt_interface_test, battery 2026-10-05) |
| 15-02-03 | 02 | 1 | D-04, D-05 / PNET-REG | T-15-06 | per-key signer set resolves from associated registry; TPR logic unchanged | unit | `ctest -R peer_registry` | ✅ `test/src/peerregistry/peer_registry_test.cpp` | ✅ green (battery 2026-10-05) |
| 15-03-01 | 03 | 2 | D-03, D-06 / PNET-NETREG | T-15-07..10 | TPR-majority bootstrap; double quorum floor; cached-only signer resolution | build+unit | `cmake --build build/OSX/Release --target networkregistry` (Unix-Makefiles) | ✅ `src/networkregistry/NetworkRegistry.{hpp,cpp}` | ✅ green — battery binaries compiled from current source; registry exercised by network_registry_test (green) |
| 15-03-02 | 03 | 2 | D-03, D-06 / PNET-NETREG | T-15-07..09 | under-signed bootstrap never confirms; no unilateral admission; no raw key bytes in records | unit | `ctest -R network_registry` | ✅ `test/src/networkregistry/network_registry_test.cpp` | ✅ green (battery 2026-10-05) |
| 15-04-01 | 04 | 2 | D-07 / PNET-GATE | T-15-11..13 | ~~allow-list predicate at all peer-aware intercept stages; deny wins; raw-stage pass-through~~ | unit (vendored) | ~~vendored `gossip_pubsub_test` + install grep~~ | — | **SUPERSEDED** — plan 15-04 permanently descoped by owner order 2026-09-03 (no 3rdparty modifications; 15-VERIFICATION override 2). D-07 delivered at the application layer by 15-11..15-15: NetworkMembershipFilter + broadcaster OnMessage ingest gate + processing-host binding. Covered by `network_membership_filter_test` + `processing_core_gating_test` (both green in battery 2026-10-05) |
| 15-04-02 | 04 | 2 | D-07 / PNET-GATE | T-15-11 | ~~GossipPubSub surface installed; SGNUS still builds against refreshed install~~ | integration | ~~`grep SetMembershipAllowList <install>/include/...`~~ | — | **SUPERSEDED** — same descoped vendored-gater surface; replacement coverage as 15-04-01 above (no vendored install exists to grep; enforcement lives in `src/networkregistry/NetworkMembershipFilter.hpp` + `src/crdt/globaldb/pubsub_broadcaster_ext.*`) |
| 15-05-01 | 05 | 3 | D-06, D-07 / PNET-GATE | T-15-15..18 | private node binds registry membership to gossip host; fail-closed on empty registry | integration | `ctest -R "trustedpeer|securecrdt"` + grep gates | ✅ (wiring; suite created: `private_network_registry_binding_test`) | ✅ green (trustedpeerregistry_genesis_test + securecrdt_interface_test + private_network_registry_binding_test, battery 2026-10-05). D-07 enforcement half delivered app-layer per supersession above |
| 15-05-02 | 05 | 3 | D-07 / PNET-GATE | T-15-15, T-15-16 | same-PSK-not-in-registry rejected; empty-membership fail-closed; runtime admission works | integration | `ctest -R pubsub_counts` (delivered case: `PnetIsolationAndGaterBlocking` — 4-host pnet isolation + gater blocking) | ✅ (extend) | ✅ green (pubsub_counts_test, battery 2026-10-05). Membership-predicate half superseded by app-layer gates (15-11..15-15) |
| 15-06-01 | 06 | 2 | D-08 / PNET-SCOPE | T-15-19..21 | scope helpers (keys/result-keys/paths/topics/chain-ids); public output byte-identical; no SetNetworkId | unit | build + grep gate | ✅ (source) | ✅ green — scoped data path grep-verified 2026-10-05: 0 `SetNetworkId` in `src/processing/` + `src/transaction/` (the 3 src/ hits are the pre-existing libp2p `sgns::version::SetNetworkId` identify-protocol knob in `src/base/`); escrow scope via `SetChainIdOverride(ScopedChainId(...))` at `src/transaction/TransactionManager.cpp:1109` |
| 15-06-02 | 06 | 2 | D-08 / PNET-SCOPE | T-15-19, T-15-19b, T-15-19d | all 15 TaskKeys sites in TaskQueueImpl + results/ keys + AddListenTopic aligned to the scoped channel; grid channel (DHT CID + StartProcessing) scoped via ScopedProcessingGridChannel; enqueue/result writes land ONLY under /chain/<id>/ (data-path placement asserted) | unit+integration | `ctest -R "task_keys_scope|task_queue"` + grep gates | ✅ `test/src/processing/task_keys_scope_test.cpp` | ✅ green (task_keys_scope_test + task_queue_test, battery 2026-10-05); real-data-path placement additionally machine-checked by 15-18 case (11) |
| 15-06-03 | 06 | 2 | D-02, D-08 / PNET-SCOPE | T-15-19c, T-15-20 | escrow CRDT path scoped + symmetric PayEscrow read; escrow chain-id override routed to genius validator; public byte-identical | unit+integration | `ctest -R "task_keys_scope|transaction_manager|startup|node"` | ✅ (extend lifecycle test) | ✅ green — task_keys_scope_test + transaction_manager_pending_lifecycle_test in battery 2026-10-05; the `startup|node` regex tail is intentionally outside the battery (phase-13 quorum-broken suites, excluded by design per 15-VERIFICATION.md) |
| 15-07-01 | 07 | 4 | D-09 / PNET-VAL | T-15-22, T-15-23 | instance-scoped identifiers; public strings byte-stable | unit | `ctest -R "blockchain|validator|genesis|multi_account"` | ✅ | ✅ green — validator_registry_scope_test (battery 2026-10-05) pins public byte-stability + scoped suffixing; blockchain/genesis regex members are phase-13 quorum-broken and excluded by design (15-VERIFICATION.md) |
| 15-07-02 | 07 | 4 | D-09 / PNET-VAL | T-15-22 | Blockchain/GossipNode scope threading; all call sites compile | integration | `ctest -R "startup|node|blockchain"` | ✅ | ✅ green — compile scope proven by the current-source build (battery binaries); validator_registry_scope_test green in battery 2026-10-05; startup/node/blockchain suites excluded (phase-13 quorum regression, documented) |
| 15-07-03 | 07 | 4 | D-09 / PNET-VAL | T-15-22, T-15-23 | three-way disjoint scope identifiers | unit | `ctest -R validator_registry_scope` | ✅ `test/src/blockchain/validator_registry_scope_test.cpp` | ✅ green (battery 2026-10-05) |
| 15-08-01 | 08 | 5 | D-11 / PNET-PROC | T-15-25..27 | processing host Noise-only + pnet + gater; errors not exceptions | unit+integration | `grep -c Plaintext == 0 && ctest -R processing` | ✅ (source) | ✅ green — grep 2026-10-05: sole `Plaintext` match is a doc comment (`processing_core_impl.hpp:84`), 0 code usages; processing suites in battery green (processing_core_gating/processing_service/processing_subtask_queue_channel_pubsub); `processing_nodes_test` excluded (phase-13 quorum regression) |
| 15-08-02 | 08 | 5 | D-11 / PNET-PROC | T-15-25, T-15-27 | injector builds public/pnet hosts; invalid key throws eagerly; gater rejects non-member | unit | `ctest -R processing_core_gating` | ✅ `test/src/processing/processing_core_gating_test.cpp` | ✅ green (battery 2026-10-05, incl. post-cycle-4 crypto-provider binding) |
| 15-09 | 09 | GC | D-06 / PNET-NETREG | — | NetworkRegistry lifecycle hardening: drain-once refresh loop (WR-02), non-destructive duplicate `New` (WR-03), `RegisterFilters` re-run in `New` (WR-04) | unit | `ctest -R network_registry_test` | ✅ (extended `test/src/networkregistry/network_registry_test.cpp`) | ✅ green (battery 2026-10-05) |
| 15-10 | 10 | GC | D-01, D-02 / PNET-CFG | — | fail-closed provisioning chain: full JSON string escaping in `WriteNetworkConfig` + fatal parse-error branch in `LoadNetworkConfig` (closes silent public-boot chain) | unit | `ctest -R network_config_private_network_test` | ✅ (extended `test/src/account/network_config_private_network_test.cpp`) | ✅ green (battery 2026-10-05) |
| 15-11 | 11 | GC | D-03, D-07 / PNET-GATE | — | application-layer membership enforcement (15-04 replacement): `MembershipFilter` + `MakeNetworkMembershipFilter` + `AuthorizeGossipSender`; broadcaster `OnMessage` dual-identity ingest gate, fail-closed | unit | `ctest -R network_membership_filter_test` | ✅ (created `src/networkregistry/NetworkMembershipFilter.hpp`, `test/src/networkregistry/network_membership_filter_test.cpp`) | ✅ green (battery 2026-10-05) |
| 15-12 | 12 | GC | D-01, D-03, D-06, D-07 / PNET-GATE | — | node-level wiring: registry-backed filter installed before READY; live membership refresh via `tx_globaldb_`; clean teardown clear | integration | `ctest -R private_network_registry_binding_test` | ✅ (created `test/src/account/private_network_registry_binding_test.cpp`) | ✅ green (battery 2026-10-05) |
| 15-13 | 13 | GC | D-08, D-11 / PNET-PROC | — | processing-path membership gates at three real handlers (grid channel, results channel, queue channel); set-time propagation + creation-time application (no enrollment window) | unit+integration | `ctest -R "processing_service_test|processing_subtask_queue_channel_pubsub_test"` | ✅ (both targets registered standalone, `test/src/processing/CMakeLists.txt`) | ✅ green (both in battery 2026-10-05) |
| 15-14 | 14 | GC | D-07, D-11 / PNET-GATE, PNET-PROC | — | authenticated sender identity at all four gates (CR-G01): keypair-sealed envelope, authenticate-before-authorize, seal-or-fail-closed publish; public nodes byte-identical | unit+integration | `ctest -R "network_membership_filter_test|processing_service_test|processing_subtask_queue_channel_pubsub_test"` | ✅ (created `src/base/gossip_auth.hpp`; extended the three suites) | ✅ green (all three in battery 2026-10-05) |
| 15-15 | 15 | GC | D-07, D-11 / PNET-GATE, PNET-PROC | — | creation-time filter install before first subscription (CR-G02a) + `MakeBootstrapMembershipFilter` interim gate before first `AddListenTopic` (CR-G02b/G-WR-03) + IN-01 warn-on-skip | unit+integration | `ctest -R "network_membership_filter_test|processing_subtask_queue_channel_pubsub_test"` | ✅ (extended both suites) | ✅ green (both in battery 2026-10-05) |
| 15-16 | 16 | GC | D-06 / PNET-NETREG | — | owner-scoped teardown: ingest-filter removal on unregister (G-WR-01), `RegisterCrdtChangeCallback` bool + fail-closed New (G-WR-02), atomic `RegisterIfAbsent` (G-WR-04) | unit | `ctest -R network_registry_test` | ✅ (extended `test/src/networkregistry/network_registry_test.cpp`) | ✅ green (battery 2026-10-05) |
| 15-17 | 17 | GC | D-06, D-07 / PNET-NETREG, PNET-GATE | — | fail-closed teardown: policy-stack failure paths leave deny-all filter on a still-live GlobalDB (CR-C2-01); element filters reject on weak-self expiry (WR-C2-01); gated `ShutdownNodePolicyServices` | unit+integration | `ctest -R "private_network_registry_binding_test|network_registry_test"` | ✅ (extended both suites) | ✅ green (both in battery 2026-10-05) |
| 15-18 | 18 | GC | D-02, D-07, D-08 | — | UAT-1: E2E two-node private-network job flow with public-node control — real-data-path scoped replication + data-level public isolation + deny-all teardown on a live member | integration | `ctest -R network_membership_filter_test` (case 11) | ✅ (extended `test/src/networkregistry/network_membership_filter_test.cpp`) | ✅ green (battery 2026-10-05; case runtime within the 33.80s suite pass) |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky · SUPERSEDED = obligation delivered by a different mechanism (descoped plan; pointer recorded in-row)*

*Every automated command runs under 300s per the sampling contract; the 15-01-02 checkpoint was the only manual gate (decision, not verification) and is resolved. Rows whose original regex spans `startup|node|blockchain|genesis|processing_nodes` suites are confirmed via the battery suites that phase 15 owns; the excluded suites are broken by the pre-existing phase-13 quorum-policy regression (documented in 15-VERIFICATION.md) and are deliberately out of battery scope.*

---

## Wave 0 Requirements

*Verified against the tree and checked off by the 2026-10-05 validation audit; all wave-0 scaffolds materialized (the vendored-gater item is closed as superseded, not delivered).*

- [x] Reconfigure `build/OSX/Release` against `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release` — **15-01 Task 1** — ✅ `build/OSX/Release/CMakeCache.txt` present (Unix-Makefiles generator); battery binaries current with HEAD source
- [x] New test suite `network_config_private_network_test` (config identity) — **15-01 Task 3** — ✅ `test/src/account/network_config_private_network_test.cpp` (registered as ctest #29, green in battery)
- [x] New test suite `peer_registry_test` (registry association) — **15-02 Task 3** — ✅ `test/src/peerregistry/peer_registry_test.cpp` (ctest #57, green)
- [x] New test suite `network_registry_test` (bootstrap/self-governance/secret exclusion) — **15-03 Task 2** — ✅ `test/src/networkregistry/network_registry_test.cpp` (ctest #66, green)
- [x] ~~Vendored allow-list gater tests + reinstall — **15-04 Tasks 1-2**~~ — **SUPERSEDED**: plan 15-04 permanently descoped by owner order 2026-09-03 (no 3rdparty modifications; 15-VERIFICATION override 2). The D-07 obligation this scaffold served was delivered at the application layer by 15-11..15-15 (`network_membership_filter_test` + `processing_core_gating_test`, both green in battery)
- [x] Extend `test/src/pubsub_counts/pubsub_counts.cpp` with the D-07 membership layer — **15-05 Task 2** — ✅ delivered as `PnetIsolationAndGaterBlocking` (4-host pnet isolation + gater blocking; ctest #133, green). The membership-predicate half evolved into the app-layer gates (see supersession above)
- [x] New test suite `task_keys_scope_test` (helper goldens + data-path placement cases) — **15-06 Task 2**; escrow cases appended — **15-06 Task 3** — ✅ `test/src/processing/task_keys_scope_test.cpp` (ctest #110, green)
- [x] New test suite `validator_registry_scope_test` — **15-07 Task 3** — ✅ `test/src/blockchain/validator_registry_scope_test.cpp` (ctest #84, green)
- [x] New test suite `processing_core_gating_test` — **15-08 Task 2** — ✅ `test/src/processing/processing_core_gating_test.cpp` (ctest #114, green)

*Existing infrastructure (CTest) covers framework needs; new suites are content, not framework. Each Wave-0 scaffold is created inside its owning task (tdd-style behavior-first where applicable) so no task lacks an automated gate.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| None anticipated | — | — | All gating/isolation behaviors should be automatable via local multi-node test binaries |

*All phase behaviors have automated verification (target state; planner to confirm no residual manual items).*

---

## Coverage Criterion (PNET-TEST) — Descope Record (CONFIRMED 2026-10-05)

**Owner acceptance recorded 2026-10-05:** the descope below was reviewed and ACCEPTED by the owner (henrique). The numeric >=80% coverage target is not measured for phase 15; the compensating control (per-task automated gates + regression battery) stands as the coverage proxy. No follow-up llvm-cov instrumentation task was requested.

The SuperGenius#367 acceptance criterion ">=80% coverage on new code" (cited in the RESEARCH requirements table) is **not measured in this phase — a recorded scope/budget descope, not an impossibility**. A measurement vehicle exists one flag away: `build/CommonCompilerOptions.cmake:50-63` defines `option(ENABLE_COVERAGE "Build with coverage analysis enabled" OFF)` with the clang/gcc instrumentation flags (`-fprofile-instr-generate -fcoverage-mapping`) and linker options already wired; the phase's `build/OSX/Release` tree is simply configured with the option OFF, and no llvm-cov report target exists. Producing a real number requires a fresh, separately-configured instrumented build tree plus a full rebuild and profiling runs — instrumenting the Release tree in place would invalidate the 15-01 Task 1 build binding, and the instrumented rebuild + profile cycle exceeds the 300s per-command feedback budget. *(Premise corrected 2026-09-01 per checker round 2: an earlier draft claimed no instrumentation flags existed anywhere in the CMake config — that claim was false.)*

- **Descoped:** the numeric >=80% target is NOT measured in this phase. This is a recorded descope, not a silent drop.
- **Compensating control:** every new behavior lands in a dedicated suite with a per-task automated gate (map above); suite pass/fail plus the pre-existing regression suites (`task_queue`, `transaction_manager`, `trustedpeer`, `securecrdt`, `pubsub_counts`, `blockchain`, `processing`) are the coverage proxy.
- **Owner confirmation:** ACCEPTED 2026-10-05 (this session). The descope is closed; no llvm-cov follow-up task pending.

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies (all 8 plans; 15-01-02 is a decision checkpoint, not a verification gap)
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references (each scaffold created inside its owning task)
- [x] No watch-mode flags
- [x] Feedback latency < 300s (per-command estimate; heaviest suites are `transaction_manager` and `pubsub_counts`)
- [x] `nyquist_compliant: true` set in frontmatter (set by planner 2026-09-01)

**Approval:** planner sign-off complete 2026-09-01; owner ACCEPTED the coverage descope 2026-10-05 (see above). Post-execution reconciliation audit 2026-10-05: all map rows green/superseded, wave 0 verified complete, frontmatter `status: ready` / `wave_0_complete: true`.

---

## Validation Audit 2026-10-05

State A reconciliation audit of the executed, verified phase (verification cycle 5 passed 11/11 at bb2f6fd42; only docs commits and two re-verified test-hygiene fixes since).

| Metric | Count | Detail |
|--------|-------|--------|
| Gaps found | 6 | stale statuses (20 rows); 15-04 rows left as MISSING; plans 09-18 absent from map; descope section pending confirmation; frontmatter draft/incomplete; no audit trail |
| Gaps resolved | 6 | all six closed in this pass (see below) |
| Gaps escalated | 0 | no implementation or test failures encountered |

- **Confirmation battery:** 15/15 suites passed, 0 failed (348.37s, exit 0) — `ctest --test-dir build/OSX/Release -R "<15-suite regex>" --output-on-failure --timeout 300`, run 2026-10-05 against binaries rebuilt from the post-hygiene-fix source state (85375ef1f/05a2c00f5; binary mtimes 11:54-12:02 postdate both commits). Heaviest suite: processing_subtask_queue_channel_pubsub_test 262.85s; network_membership_filter_test 33.80s.
- **Row updates:** 15-01-02 checkpoint marked resolved (decision recorded in 15-01-SUMMARY); all other 01-08 rows flipped to ✅ green with battery evidence; rows 15-04-01/02 rewritten as SUPERSEDED (app-layer delivery by 15-11..15-15); ten consolidated rows appended for plans 09-18 with frontmatter requirement IDs.
- **Source-gate spot checks (run 2026-10-05):** `SetNetworkId` — 0 hits in the scoped data path (`src/processing/`, `src/transaction/`); the 3 src/ hits are the pre-existing `sgns::version::SetNetworkId` identify-protocol knob. `Plaintext` — sole match is a doc comment (`processing_core_impl.hpp:84`); 0 code usages. Escrow scope shape verified at `src/transaction/TransactionManager.cpp:1109` (`SetChainIdOverride(ScopedChainId(...))`).
- **Caveat (not a failure):** rows whose original regex spans `startup|node|blockchain|genesis|processing_nodes` suites were confirmed via the phase-owned battery suites; the excluded suites are broken by the pre-existing phase-13 quorum-policy regression and are deliberately out of battery scope (documented in 15-VERIFICATION.md).
