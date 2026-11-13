---
phase: 18-txpruner-service-first-real-prune
plan: 02
subsystem: account
tags: [dev-mint-policy, d-03, net-scoping, witness-bypass, input-validator, consensus, c++17]

# Dependency graph
requires:
  - phase: 18-txpruner-service-first-real-prune
    provides: none (independent of 18-01's D-02 guard; no file overlap)
provides:
  - DevMintBurnlessPermitted() + kDevMintPermittedNets — the ONE compile-time home for the dev-mint net rule (src/base/DevMintPolicy.hpp, beside the sgns_version net constants)
  - DevMintInputValidator registered for chain id "dev" (production twin of TestMintInputValidator; kValid iff the validator's OWN net is designated)
  - Net-scoped evidence bypass — both GatherVerificationEvidence legs refuse on non-designated nets with default-invalid evidence
  - The dev_mint_policy_test suites (EvidenceBypass, Validator, DesignatedNet real quorum, Refuses) — reusable for staging-demo verification
affects: [PRUNE-04 enablement (staging seeding of prunable history), 18-05 GeniusNode wiring, milestone security posture]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Net-scoped policy predicate: reads ONLY version::GetNetworkID(), takes no arguments — the validator's own process-global net decides, never a proposer claim (Pitfall 5 made grep-verifiable)"
    - "Bypass-leg extraction: the two legacy bypass legs moved into a file-scope LocalBypassEvidence helper returning optional<RpcVerificationEvidence> — keeps the literal per-leg conjunct AND GatherVerificationEvidence under the cognitive-complexity threshold"
    - "Process-global net fixture discipline: save GetNetworkID in SetUp, SetNetworkId BEFORE peer construction (topics carry the net appendix), restore in TearDown"

key-files:
  created:
    - src/base/DevMintPolicy.hpp
    - src/account/DevMintInputValidator.hpp
    - src/account/DevMintInputValidator.cpp
    - test/src/account/dev_mint_policy_test.cpp
  modified:
    - src/account/PublicChainInputValidator.cpp
    - src/transaction/TransactionManager.cpp
    - src/account/CMakeLists.txt
    - src/base/CMakeLists.txt
    - test/src/account/CMakeLists.txt

key-decisions:
  - "Designated-net set is compile-time constexpr (staging 333 + dev 144, built from the named constants) — config can never widen it; only a code change can add an allowance"
  - "Dev validator's ValidateUTXOParameters keeps the twin's non-empty-params shape — param binding is re-checked against the subject's commitment by HandleNonceConsensusSubject; strictness there would duplicate it"
  - "The consensus suites drive the FULL public Blockchain surface (CreateConsensusProposal/SubmitProposal/CheckCertificateForSlot) — zero friend-access needed, unlike the multi_node_finality_fault analog"
  - "Refusal windows are 10s negatives against the designated case's ~6s positive — waitForCondition (base/util.hpp) returning false IS the refusal proof"

patterns-established:
  - "Four-peer real-consensus fixture driven entirely through Blockchain's public proposal API (no TestAccess friend class)"
  - "Negative wait discipline: EXPECT_FALSE(waitForCondition(cert-appears, window)) — the condition-never-became-true timeout is the assertion"

requirements-completed: []  # PRUNE-04 enabled (staging can now seed prunable history) but its crash-consistent cycle lands with TxPruner in 18-03/18-04 — checkbox deferred per the Phase-16/17 precedent

# Metrics
duration: 43min
completed: 2026-10-21
---

# Phase 18 Plan 02: D-03 Net-Scoped Dev-Mint Policy Summary

**Burn-less mints now certify through FULL consensus only on the designated nets (staging 333 + dev 144) — a compile-time constexpr policy gates the dev validator's verdict and both GatherVerificationEvidence bypass legs, closing the unconditional empty-chain-id hole on every other net and the witness-bypass-hardening todo by design.**

## Performance

- **Duration:** 43 min
- **Started:** 2026-10-21T18:16:38Z
- **Completed:** 2026-10-21T18:59:55Z
- **Tasks:** 2 (TDD: RED + GREEN for Task 1, then the real-consensus suites + todo closure)
- **Files:** 9 created/modified (4 created, 5 modified) + todo relocation

## Accomplishments

### Task 1: DevMintPolicy + DevMintInputValidator + net-scoped bypass + registration (TDD)

- **RED** (9f802a1f1): 8 unit tests across PolicyPermits / EvidenceBypass (all three legs) / DevValidator suites + the CMake registration; build failed on the missing `base/DevMintPolicy.hpp` / `account/DevMintInputValidator.hpp` (18-01 RED precedent).
- **GREEN** (f8d65d1c):
  - `src/base/DevMintPolicy.hpp`: `kDevMintPermittedNets` built from `version::STAGING_NET_ID`/`version::DEV_NET_ID` (zero raw net literals — grep-verified) + `DevMintBurnlessPermitted()` reading only `version::GetNetworkID()`.
  - `DevMintInputValidator.{hpp,cpp}`: production twin of `TestMintInputValidator`; `ValidateWitness` kValid iff `DevMintBurnlessPermitted()` (ignores witness content entirely); `Register()` claims the literal chain id `"dev"` (insert-only); `RequiresConsensusUTXOData()` false.
  - `GatherVerificationEvidence`: both bypass legs gated; refusal returns the default-invalid evidence untouched and logs the refused net; permitted-path log lines preserved verbatim.
  - `TransactionManager.cpp`: `kDevMintValidatorRegistered` anchor beside `kGeniusValidatorRegistered` (same linker rationale).
  - CMake: `DevMintInputValidator.cpp` in `GENIUS_NODE_SOURCES`; `DevMintPolicy.hpp` in the `sgns_version` target's header list (the target enumerates headers).

### Task 2: Real-consensus suites + todo closure (268e3228f)

- `DevMintConsensusTest`: four-peer real GossipPubSub + GlobalDB + quorum harness (multi_node_finality_fault shape: sequential-join dialing, advertised loopback address, scheduler on the host's io_context, db-before-pubsub teardown), driven through Blockchain's public proposal surface.
- **DesignatedNet** (net 333): the `"dev"`-chain burn-less mint certifies — certificate on every peer through CRDT within 20s (actual ~6s), binds exactly the mint (nonce-subject tx hash), produced UTXO lands on every peer.
- **Refuses** (net 963): the identical subject never certifies in a 10s window and produces no UTXO; the empty-chain-id mint (the actually-reachable hole: routes "public", no RPC endpoints) is refused there too — through the scoped bypass.
- Fixture discipline: net saved in SetUp / restored in TearDown; set BEFORE construction (topics are net-suffixed). Existing suites in the binary unaffected (full 11/11 run).
- A6 collision check: `"dev"` collides with no chainlist chain id (chainlist/bridge ids are numeric strings or hyphenated names; `bridge_chains_config.json` keys are names like `ethereum-mainnet`).
- Todo `genius-chain-witness-bypass-hardening` → `resolved-by-design` with the full resolution record, moved to `.planning/todos/completed/`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] RED test helper returned the abstract base where a concrete type was needed**
- **Found during:** Task 1 GREEN build
- **Issue:** `MintWithChain` returned `shared_ptr<GeniusTransaction>`; `GetUTXOParameters()` is defined on `MintTransactionV2`, so the params test failed to compile.
- **Fix:** Helper returns `shared_ptr<MintTransactionV2>` (implicit upcast still satisfies the `GeniusTransaction&` parameters).
- **Files modified:** test/src/account/dev_mint_policy_test.cpp
- **Commit:** f8d65d1c8

**2. [Rule 1 - Bug] Bypass-leg gating pushed GatherVerificationEvidence over the cognitive-complexity threshold**
- **Found during:** Task 1 tooling gate (clang-tidy)
- **Issue:** The nested refusal branches took the function from clean to complexity 45 (threshold 40, [Recommended] check) — my changed lines triggered a new warning.
- **Fix:** Extracted the two bypass legs verbatim into a file-scope `LocalBypassEvidence` helper returning `optional<RpcVerificationEvidence>` (complexity ~7); GatherVerificationEvidence is now BELOW its pre-change complexity. Leg order, log lines, refusal shape, and the literal per-leg conjunct all preserved (grep: exactly 2 `DevMintBurnlessPermitted` occurrences in the file).
- **Files modified:** src/account/PublicChainInputValidator.cpp
- **Commit:** f8d65d1c8

**3. [Rule 3 - Blocking] Consensus harness built on the friend-access analog had no legal access path**
- **Found during:** Task 2 harness design
- **Issue:** The analog reaches the ConsensusManager through `MultiNodeFinalityFaultTestAccess` (friend of Blockchain) — adding another friend would modify Blockchain.hpp, outside this plan's files.
- **Fix:** Drove the entire flow through Blockchain's existing public surface (`CreateConsensusProposal` — which internally binds the registry Cid/Epoch exactly like the analog, `SubmitProposal`, `CheckCertificateForSlot`, `GetCertificateBySlot`) plus public `ConsensusManager::GetSlotKey`/`DecodeNonceSubject`. Zero production-header changes; behavior identical.
- **Files modified:** test/src/account/dev_mint_policy_test.cpp
- **Commit:** 268e3228f

**4. [Rule 1 - Bug] PRUNE-04 checkbox ticked prematurely by the requirements step**
- **Found during:** State updates (`requirements.mark-complete PRUNE-04` per the plan frontmatter)
- **Issue:** PRUNE-04's wording is the idempotent crash-consistent prune CYCLE — delivered by the TxPruner plans (18-03/18-04). This plan is the requirement's *enablement* (real prunable history can now be seeded on staging); ticking it Complete would make REQUIREMENTS.md claim a cycle that does not exist yet.
- **Fix:** Reverted checkbox + traceability row to Pending — the Phase-16/17 mid-phase checkbox-deferral precedent recorded in STATE decisions.
- **Files modified:** .planning/REQUIREMENTS.md
- **Commit:** docs(18-02) final metadata commit

## TDD Gate Compliance

- RED gate: `test(18-02)` commit 9f802a1f1 — build failure on the missing policy/validator headers (verified in execution log).
- GREEN gate: `feat(18-02)` commit f8d65d1c8 after 8/8 unit tests + the 4 existing consensus/certificate ctest suites passed.
- REFACTOR gate: folded into GREEN (the LocalBypassEvidence extraction, deviation 2) — tests re-run green after it.

## Verification Evidence

- `cmake --build build/OSX/Release --target dev_mint_policy_test genius_transaction_objs genius_node` — clean.
- `build/OSX/Release/test_bin/dev_mint_policy_test` (full binary) — 11/11 PASSED, twice consecutively (double-run).
- `ctest --test-dir build/OSX/Release -R "certificate_fallback|multi_node_finality"` — 4/4 passed, run after Task 1 GREEN and again after the helper refactor.
- `/opt/homebrew/opt/llvm@22/bin/git-clang-format --diff HEAD` — "did not modify any files" at each commit.
- `/opt/homebrew/opt/llvm@22/bin/clang-tidy` (filtered single-arch db) on DevMintInputValidator.cpp / PublicChainInputValidator.cpp / TransactionManager.cpp — no Required findings on changed lines; remaining warnings are pre-existing (verified against the pre-change file) or [Recommended]-group items matching established repo patterns (`static ... instance` mirrors `GeniusInputValidator::Register` verbatim).
- Acceptance greps: zero raw net literals in DevMintPolicy.hpp; predicate parameterless; 2 conjunct occurrences; `Register("dev", ...)` + anchor present; no file deletions in any commit.

## Threat Mitigations Landed

| Threat | Disposition |
|--------|-------------|
| T-18-0201 (empty-chain-id mint certifying on main) | mitigated — both legs net-scoped; Refuses suite proves the empty-chain-id mint rejected on 963 (valid=false asserted at unit level too) |
| T-18-0202 (policy keyed on proposer claims) | mitigated — predicate reads only GetNetworkID(), parameterless (grep-verified); Validator suite proves identical witness bytes refused on 963/369 |
| T-18-0203 (config widening the designated set) | mitigated — compile-time constexpr set; header documents that only a code change widens it |
| T-18-0204 (spoofing the designated net) | accepted — claiming 333 means joining net 333's topics/keyspace (consensus/datastore topics are net-suffixed; cross-net quorums cannot form) |
| T-18-0205 ("dev" registry collision) | mitigated — insert-only registration; A6 verified no chainlist collision |
| T-18-0206 (dev mints indistinguishable) | mitigated — chain id "dev" and the empty witness are on-chain facts; full consensus/quorum/certificate path unchanged |

## Self-Check: PASSED

- Files exist: src/base/DevMintPolicy.hpp, src/account/DevMintInputValidator.{hpp,cpp}, src/account/PublicChainInputValidator.cpp, src/transaction/TransactionManager.cpp, test/src/account/dev_mint_policy_test.cpp, .planning/todos/completed/genius-chain-witness-bypass-hardening.md, 18-02-SUMMARY.md — all FOUND.
- Commits exist on branch: 9f802a1f1 (test RED), f8d65d1c8 (feat GREEN), 268e3228f (test suites + todo) — all FOUND via `git log`.
