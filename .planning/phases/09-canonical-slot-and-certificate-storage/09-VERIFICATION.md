---
phase: 09-canonical-slot-and-certificate-storage
verified: 2026-07-24T16:23:12Z
status: gaps_found
score: "57/60 must-have truths verified"
requirements: "10/10 phase requirements satisfied"
roadmap_success_criteria: "4/4 verified"
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: "38/43 must-have truths verified; 8/10 requirements satisfied"
  gaps_closed:
    - "Catch-up now uses Processed, AlreadyHandled, and Retry outcomes; callback Retry/exception preserves cursor and dedup state."
    - "Local and replicated certificate write preflight now treat only exact datastore NOT_FOUND as absence."
    - "Mixed Reject plus RetryDependency filtering now preserves the retained dependency barrier and fails closed on distinct dependencies."
  gaps_remaining: []
  regressions:
    - "A bridge executed marker and CONFIRMED state can become durable before mint transaction effects complete."
    - "Worker-owned CRDT destruction is not lifetime-safe when the final strong owner disappears on an internal worker."
    - "Runtime RPC endpoint mutation races with vote and receipt-validation reads."
gaps:
  - truth: "Catch-up advances only on durable proof that the burn's transaction effects have completed."
    status: failed
    reason: "ChangeTransactionState(CONFIRMED) records CONFIRMED and the executed-burn marker before ParseTransaction applies produced UTXOs and consumes the bridge input. A later effect failure is therefore reported after durable completion markers exist; duplicate certificate delivery skips ParseTransaction, and catch-up classifies the marker as AlreadyHandled."
    artifacts:
      - path: "src/account/TransactionManager.cpp"
        issue: "Lines 4647-4706 publish CONFIRMED/executed state before ParseTransaction; lines 1810-1821 can fail while applying mint-v2 effects."
      - path: "test/src/account/transaction_manager_pending_lifecycle_test.cpp"
        issue: "No produced-UTXO or bridge-input fault-injection test proves duplicate certificate delivery/restart completes interrupted effects."
    missing:
      - "An idempotent or transactional application boundary that completes/verifies transaction effects before publishing CONFIRMED and the executed-burn marker, or a durable applying state that resumes safely."
      - "Fault-injection coverage for produced-UTXO and bridge-input failures followed by duplicate certificate delivery and restart."
  - truth: "Worker-initiated CRDT shutdown remains lifetime-safe when the last external owner is released by a worker."
    status: failed
    reason: "Worker loops temporarily promote weak ownership. If the final strong owner disappears on a worker, the destructor calls Close; RequestClose starts a joinable member thread capturing raw this, while CancelAndCloseNow returns on an internal worker. The member thread can cause std::terminate during destruction or dereference freed state."
    artifacts:
      - path: "src/crdt/impl/crdt_datastore.cpp"
        issue: "Lines 847-850, 897-948, and 990-997 do not provide a safe last-owner-on-worker destruction path."
      - path: "test/src/crdt/crdt_datastore_test.cpp"
        issue: "WorkerInitiatedShutdownCompletesBeforeBarrierAndRunsNoPostCloseWork retains an external datastore owner and does not exercise final-owner release from the worker."
    missing:
      - "A lifetime-safe shutdown control state whose coordinator does not capture a destructing CrdtDatastore through raw this."
      - "A subprocess/death-safe regression releasing the last external datastore owner from a worker callback."
  - truth: "Each vote and receipt-validation decision uses one stable RPC endpoint configuration snapshot."
    status: failed
    reason: "SetRpcEndpoints and AddRpcEndpoints mutate rpc_endpoints_ while GetSlotHash, GetFirstConfiguredChainId, GetFirstRpcUrl, and VerifyPublicChainSmartContract read the same unordered_map/vectors without synchronization. ConfigureRpcEndpoint can run while asynchronous provider initialization publishes fetched endpoints and consensus reads slot hashes."
    artifacts:
      - path: "src/account/PublicChainInputValidator.hpp"
        issue: "rpc_endpoints_ and transport_factory_ have no lock or immutable snapshot ownership."
      - path: "src/account/PublicChainInputValidator.cpp"
        issue: "Lines 155-238 mutate containers while lines 241-318 and 412-437 read/iterate them unsafely."
      - path: "src/account/GeniusNode.cpp"
        issue: "ConfigureRpcEndpoint and asynchronously posted provider initialization can publish endpoint changes concurrently with slot-hash and validation readers."
    missing:
      - "Reader/writer synchronization or atomically published immutable per-chain snapshots, held for one complete decision."
      - "A deterministic concurrency regression covering blocked provider initialization, operator configuration, slot-hash reads, and receipt quorum reads."
deferred:
  - truth: "Two certificates concurrently formed from empty distributed state cannot both race through local write-once preflight."
    addressed_in: "Phase 10"
    evidence: "Phase 10 goal and success criterion 1 require a durable one-signature-per-slot vote lock. Phase 9 research explicitly states that CRDT storage provides no distributed compare-and-swap and assigns concurrent formation safety to Phase 10."
---

# Phase 09: Canonical Slot and Certificate Storage Verification Report

**Phase goal:** Establish one canonical finality identity for every proposal and make slot-keyed certificates compatible with transaction-hash certificate consumers.

**Status:** `gaps_found`

**Re-verification:** Yes — after plans 09-10 through 09-13.

## Verdict

The canonical-slot and certificate-storage roadmap contract is implemented: all four roadmap success criteria and all ten Phase 9 requirements are satisfied in current source. The three historical blockers are also closed in their targeted paths.

Phase completion still fails under the verifier contract because three plan must-haves are false in supported runtime paths. A persisted bridge-executed marker is not yet proof that mint effects completed, worker-originated CRDT shutdown is unsafe when the worker releases the final owner, and endpoint decisions do not use a stable snapshot while endpoint configuration is intentionally concurrent.

The documented Phase 9/Phase 10 boundary is preserved. Phase 9 local/replicated write-once checks are verified; preventing two independently formed certificates from racing out of empty distributed state remains Phase 10's durable vote-lock responsibility, not a Phase 9 gap.

## Goal Achievement

### Roadmap Success Criteria

| # | Success criterion | Status | Evidence |
|---|---|---|---|
| 1 | Same nonce shares one normal slot; same external burn shares one bridge slot independently of candidate fields | ✓ VERIFIED | `MakeNonceSlotPreimage`, `MintTransactionV2::GetSlotPreimage`, and result-returning `ConsensusManager::GetSlotKey` produce canonical 64-lowercase-hex SHA-256 slots; `consensus_slot_key_test` passes 12/12. |
| 2 | Exact-proposal certificates are authoritative by slot and expose a verified transaction-hash index | ✓ VERIFIED | Strict normalization retains the complete proposal/votes; one batch publishes `/cert/v2/slot/<slot>` and `/cert/v2/tx/<winner>`; both local and replicated preflights fail closed. |
| 3 | Previous-nonce and producer-UTXO consumers use `GetCertificateBySubjectHash()` | ✓ VERIFIED | Both real consumers call the Blockchain wrapper; winner, absence, corruption, and I/O paths pass in `transaction_manager_pending_lifecycle_test`. |
| 4 | Legacy transaction-keyed certificate state fails startup clearly | ✓ VERIFIED | `HasCompatibleCertificateState()` executes before subscription, timers, filters, and recovery; runtime namespace/tombstone rejection prevents later poisoning. |

### Observable Truths

#### Plan 09-01 — Canonical slot identity

| # | Truth | Status | Evidence |
|---|---|---|---|
| 1 | Every accepted subject resolves to one lowercase 64-hex SHA-256 slot | ✓ VERIFIED | Domain preimages are validated and hashed; consensus revalidates the result. |
| 2 | Same canonical source and nonce share one normal slot | ✓ VERIFIED | The normal preimage is exactly canonical address plus unsigned-decimal nonce. |
| 3 | Same chain, burn hash, and receipt-local index share one mint slot despite candidate fields | ✓ VERIFIED | Mint preimage includes only those three facts. |
| 4 | Invalid/noncanonical slot input rejects without proposal fallback | ✓ VERIFIED | Registered handler failures propagate before proposal/slot-state mutation. |

#### Plan 09-02 — Canonical bridge event identity

| # | Truth | Status | Evidence |
|---|---|---|---|
| 5 | Live and catch-up burns carry absolute receipt-local position | ✓ VERIFIED | Live iteration emits the ordinal; catch-up resolves block log index against the full receipt. |
| 6 | Multiple burns in one transaction remain independently mintable | ✓ VERIFIED | Discovery and dedup use `(tx_hash, receipt_log_index)`. |
| 7 | Synthetic outpoint, reservation, rollback, and persistence identity use the index | ✓ VERIFIED | `MintFunds` uses the required index at every bridge-state seam. |
| 8 | Validators accept only the exact indexed log matching chain/token/amount/destination | ✓ VERIFIED | The validator bounds-checks and decodes only `receipt.logs[input.output_idx_]`. |

#### Plan 09-03 — Slot-keyed certificate store

| # | Truth | Status | Evidence |
|---|---|---|---|
| 9 | Publication exposes slot record and winner index in one CRDT delta | ✓ VERIFIED | One batch `GlobalDB::Put(vector<DataPair>)` emits both siblings. |
| 10 | Exact replay is idempotent; a different occupied-slot certificate rejects | ✓ VERIFIED | Shared typed preflight plus canonical byte comparison implements replay/conflict behavior. |
| 11 | Malformed, partial, or mismatched replicated pairs reject before merge | ✓ VERIFIED | Complete namespace filter checks shape, keys, values, canonical bytes, and certificate semantics. |
| 12 | Legacy transaction-keyed state blocks startup before side effects | ✓ VERIFIED | Compatibility scan precedes background consensus activity. |

#### Plan 09-04 — Verified lookup compatibility

| # | Truth | Status | Evidence |
|---|---|---|---|
| 13 | Slot lookup returns only a certificate deriving to the requested slot | ✓ VERIFIED | Canonical input, bytes, certificate, and derived slot are checked. |
| 14 | Hash lookup verifies index-to-slot and certificate-to-winner links | ✓ VERIFIED | Index lookup delegates to authoritative slot validation and checks the exact winner. |
| 15 | Losing hash is NotFound while a full losing subject observes finalized slot | ✓ VERIFIED | Hash and subject-slot APIs have separate semantics. |
| 16 | Existing consumers retrieve winners without storage-layout knowledge | ✓ VERIFIED | Account consumers use Blockchain wrappers and never read `/cert/` directly. |

#### Plan 09-05 — External mint proof closure

| # | Truth | Status | Evidence |
|---|---|---|---|
| 17 | Invalid/noncanonical burn hashes cannot mutate state or form a slot | ✓ VERIFIED | `MintFunds` validates before mutation; mint slot derivation rejects zero hashes. |
| 18 | Noncanonical source-chain IDs reject before mutation | ✓ VERIFIED | Canonical unsigned-decimal validation precedes state reads/writes. |
| 19 | Only explicit Genius chain IDs bypass external proof | ✓ VERIFIED | Empty/unknown chains reject; only the two named local IDs bypass. |
| 20 | Receipt hash must equal requested burn before endpoint weight | ✓ VERIFIED | Equality precedes status and log inspection. |

#### Plan 09-06 — Transactional catch-up receipt resolution

| # | Truth | Status | Evidence |
|---|---|---|---|
| 21 | Chunk completion requires every enabled query and receipt dependency | ✓ VERIFIED | v1/v2 query and resolution results gate publication. |
| 22 | BurnProcessor sees no candidate before full chunk validation | ✓ VERIFIED | `process_logs` stages; one later sweep publishes. |
| 23 | Missing/malformed/mismatched/ambiguous/overflowing identity preserves cursor | ✓ VERIFIED | Each branch fails the chunk and retains its start. |
| 24 | Wide receipt ordinal narrowing is checked | ✓ VERIFIED | Shared helper covers 0, UINT32_MAX, and UINT32_MAX+1. |
| 25 | Retry performs fresh receipt requests | ✓ VERIFIED | Cache scope is one `poll_once`. |
| 26 | Successful dependency resolution delivers once and advances to `chunk_end + 1` | ✓ VERIFIED | Receipt/query retries now preserve the cursor until publication outcomes are evaluated. |

#### Plan 09-07 — Canonical certificate bytes and namespace

| # | Truth | Status | Evidence |
|---|---|---|---|
| 27 | Accepted certificates have one canonical protobuf representation | ✓ VERIFIED | One strict normalizer and deterministic serializer serve all ingress/read paths. |
| 28 | Unknown/duplicate/invalid/nonordered/redundant variants reject remotely | ✓ VERIFIED | Recursive unknown checks and strict vote validation cover every variant. |
| 29 | Votes are bytewise ordered, registry-valid, and redundant fields derived | ✓ VERIFIED | Normalization derives weights/timestamp and sorts unique votes. |
| 30 | Runtime `/cert/` accepts only exact v2 pairs | ✓ VERIFIED | Full-namespace guard removes legacy/malformed records before merge. |
| 31 | Certificate tombstones are terminally rejected before merge | ✓ VERIFIED | Element/tombstone matching and exact-ID regressions pass. |

#### Plan 09-08 — Missing-registry dependency retry

| # | Truth | Status | Evidence |
|---|---|---|---|
| 32 | Registry-stalled pair is not merged, deleted, or processed | ✓ VERIFIED | Retry exits before merge and retains the unprocessed source. |
| 33 | Filtering distinguishes terminal rejection from retryable stall | ✓ VERIFIED | Tri-state decision and namespace-aware mixed aggregation preserve retained retry. |
| 34 | Reject removes its elements/tombstones while preserving unrelated namespaces | ✓ VERIFIED | Sanitized delta retains unrelated entries. |
| 35 | Stalled root parks with backoff and releases active ownership | ✓ VERIFIED | Tagged iteration result routes to bounded parked-root state. |
| 36 | Parked roots are bounded and cleaned at capacity/TTL/attempt limits | ✓ VERIFIED | Caps, deadlines, counters, cleanup, and tests are present. |
| 37 | Registry arrival causes one validated atomic merge and delivery | ✓ VERIFIED | Dependency retry and certificate filter compose to approve and deliver once. |
| 38 | Due retry cannot be starved by later arrivals or TTL | ✓ VERIFIED | Due-time queue snapshot gates later roots. |

#### Plan 09-09 — Typed reads and real consumers

| # | Truth | Status | Evidence |
|---|---|---|---|
| 39 | Only database NOT_FOUND becomes certificate NotFound | ✓ VERIFIED | Shared mapper performs exact error comparison. |
| 40 | Corruption is IntegrityError; operational failure is StorageError | ✓ VERIFIED | Fault-injection matrix covers both classes. |
| 41 | Dangling index is IntegrityError; slot I/O remains StorageError | ✓ VERIFIED | Relationship handling follows raw-read classification. |
| 42 | Real nonce-chain and producer consumers execute all outcome classes | ✓ VERIFIED | Production private paths are exercised through narrow friend access. |
| 43 | Only previous-nonce absence becomes Pending | ✓ VERIFIED | Corruption/I/O reject; producer lookup rejects every non-success. |

#### Plan 09-10 — Durable catch-up publication

| # | Truth | Status | Evidence |
|---|---|---|---|
| 44 | Chunk advances only after Processed or durable AlreadyHandled proof | ✗ FAILED | Executed-burn state can be written before mint UTXO effects complete, so it is durable but not proof of completed handling. |
| 45 | Retry and callback exceptions preserve cursor and uncommitted dedup | ✓ VERIFIED | Publication sweep stops before both commit points; direct tests pass. |
| 46 | Reservation, unavailable components, failed submission, and read failure are Retry | ✓ VERIFIED | Production classifier and friend-only tests cover each case. |
| 47 | Only persisted executed state or consumed outpoint returns AlreadyHandled | ✓ VERIFIED | `GetBridgeBurnState` and classifier enforce those two sources. |
| 48 | MintFunds treats only NOT_FOUND as absent and does not mutate on read failure | ✓ VERIFIED | Typed bridge reader gates UTXO/reservation/queue mutation. |

#### Plan 09-11 — Fail-closed certificate preflight

| # | Truth | Status | Evidence |
|---|---|---|---|
| 49 | Local submission treats only NOT_FOUND as empty | ✓ VERIFIED | Both slot/index reads use `ReadCertificatePreflightRecord`. |
| 50 | Replicated filtering rejects occupied-state read failure | ✓ VERIFIED | Integrity/storage errors return terminal Reject before dependency classification. |
| 51 | Preflight shares the lookup raw-read classifier | ✓ VERIFIED | Shared helper delegates to `MapCertificateReadError`. |
| 52 | Local/remote symmetric and asymmetric faults preserve visible state | ✓ VERIFIED | Seven-row matrices and receiver-side delivery checks pass. |

#### Plan 09-12 — Mixed CRDT decisions and shutdown completion

| # | Truth | Status | Evidence |
|---|---|---|---|
| 53 | Reject cannot erase RetryDependency for a retained namespace | ✓ VERIFIED | Aggregation evaluates original input, sanitizes separately, and retains the dependency. |
| 54 | Retained retry namespace remains invisible and parks under original CID | ✓ VERIFIED | Datastore returns before merge; regression passes. |
| 55 | Distinct retry dependencies fail closed deterministically | ✓ VERIFIED | All affected namespaces are sanitized rather than choosing one CID. |
| 56 | Shutdown queue snapshots are taken under the owning mutex | ✓ VERIFIED | One `SnapshotShutdownStateLocked` helper is used under `dagWorkerMutex_`. |
| 57 | Worker-initiated shutdown completes only after joins/drain and no later work | ✗ FAILED | The retained-owner test passes, but last-owner-on-worker destruction can spawn a raw-`this` joinable coordinator during the destructor and return without joining it. |

#### Plan 09-13 — Endpoint-local receipt status

| # | Truth | Status | Evidence |
|---|---|---|---|
| 58 | Missing/failed status contributes zero weight without vetoing later endpoints | ✓ VERIFIED | Branch increments `tried` and continues. |
| 59 | Later exact endpoints totaling 75 weight prove the mint | ✓ VERIFIED | Ordered 50+25 regression passes. |
| 60 | Receipt/hash/log/facts/weight remain endpoint-local and failing endpoints add no weight | ✗ FAILED | Sequential accumulation is correct, but runtime endpoint mutation can invalidate the referenced map/vector during a decision; there is no stable snapshot. |

**Must-have score:** **57/60**

## Required Artifacts

| Artifact group | Status | Details |
|---|---|---|
| Canonical transaction and slot code | ✓ VERIFIED | Exists, substantive, wired through transaction handlers and consensus maps. |
| Certificate normalization/store/lookups | ✓ VERIFIED | Canonical bytes, atomic pair, typed preflight, verified index, and legacy guard are wired. |
| Bridge observation/catch-up/mint construction | ⚠ PARTIAL | Identity and publication outcomes are wired, but the executed marker precedes complete effect application. |
| CRDT filter/retry/shutdown | ⚠ PARTIAL | Filter/retry behavior is substantive; final-owner worker destruction is unsafe. |
| Public-chain receipt validator | ⚠ PARTIAL | Exact endpoint-local sequential policy is implemented; concurrent configuration lacks a stable snapshot. |
| Phase test targets | ⚠ PARTIAL | All declared artifacts exist and focused suites pass; the three failing runtime paths have no direct regression. |

The GSD artifact parser recognizes all structured artifacts in Plans 09-10 through 09-13. Older plans use scalar artifact entries that the parser reports as zero structured artifacts; manual existence/substance/wiring inspection was therefore used for Plans 09-01 through 09-09. No declared artifact is missing or a stub.

## Key Link Verification

| Link | Status | Details |
|---|---|---|
| Domain transaction → canonical preimage/hash → consensus maps | ✓ WIRED | One result-returning path; registered failures do not fall back. |
| Receipt observation → relayer/catch-up → GeniusNode → MintFunds → `output_idx_` | ✓ WIRED | Receipt-local index is mandatory end to end. |
| Certificate → normalization → typed slot/index preflight → atomic batch | ✓ WIRED | Local and replicated paths share canonical validation and fail-closed reads. |
| Replicated pair → complete namespace filter → dependency retry/merge | ✓ WIRED | Mixed decisions retain retry and distinct dependencies reject. |
| Slot lookup → transaction index → Blockchain consumers | ✓ WIRED | Relationships and typed errors are verified through real consumers. |
| Catch-up outcome → durable completion → cursor commit | ✗ PARTIAL | Outcome branching is wired, but one AlreadyHandled source can precede completion of transaction effects. |
| Worker callback → close coordinator → completion barrier | ✗ PARTIAL | Retained-owner path works; final-owner worker destruction does not. |
| Runtime endpoint publication → vote/receipt validation snapshot | ✗ NOT_WIRED | Writers and readers access mutable map/vector/factory state without synchronization. |

## Data-Flow Trace

| Artifact | Data | Source → sink | Status |
|---|---|---|---|
| Canonical mint slot | chain, burn hash, receipt index | Receipt → mint input → `GetSlotPreimage` → SHA-256 → consensus maps | ✓ FLOWING |
| Certificate lookup | winning hash and slot | tx index → slot key → normalized certificate → exact winner check → consumers | ✓ FLOWING |
| Catch-up completion | publication outcome | durable bridge state/submission → outcome → dedup/cursor commit | ✗ PARTIAL |
| RPC proof | endpoint configuration | runtime writers → shared mutable container → vote/receipt readers | ✗ UNSYNCHRONIZED |

## Requirements Coverage

| Requirement | Status | Implementation and test evidence |
|---|---|---|
| SLOT-01 | ✓ SATISFIED | One deterministic result-returning slot path is used by arbitration and certificate lookup. |
| SLOT-02 | ✓ SATISFIED | Normal slot preserves source-address-plus-nonce semantics. |
| SLOT-03 | ✓ SATISFIED | Mint slot uses source chain, nonzero burn hash, and receipt-local index only. |
| SLOT-04 | ✓ SATISFIED | Proposer, nonce, amount, destination, and other candidate fields cannot split the burn slot. |
| CERT-01 | ✓ SATISFIED | Strict canonical certificate retains and validates complete winning proposal, registry, and votes. |
| CERT-02 | ✓ SATISFIED | Authoritative record is slot-keyed; write-once preflight is typed and fail closed. |
| CERT-03 | ✓ SATISFIED | Winning hash index is emitted atomically with the slot record and contains only slot ID. |
| CERT-04 | ✓ SATISFIED | Hash lookup verifies index, authoritative slot, canonical bytes, and exact embedded winner. |
| COMP-01 | ✓ SATISFIED | Real previous-nonce and producer-UTXO consumers use the verified hash index. |
| COMP-02 | ✓ SATISFIED | Startup rejects legacy state before side effects; runtime namespace guards prevent poisoning. |

No orphaned Phase 9 requirements exist. All ten roadmap IDs appear in plan frontmatter. The three current blockers arise from additional plan must-haves and operational safety contracts rather than absence of a mapped requirement implementation.

## Code Review Finding Adjudication

| Finding | Disposition | Phase 9 impact |
|---|---|---|
| CR-01 — mint confirmation before effects | **Confirmed blocker** | Invalidates Plan 09-10 truth 44: the persisted executed marker is not reliable AlreadyHandled proof until effects finish. This is not deferred by a later roadmap criterion. |
| CR-02 — worker-owned CRDT destruction | **Confirmed blocker** | Invalidates Plan 09-12 truth 57: current completion proof retains an external owner and misses last-owner-on-worker destruction. |
| CR-03 — unsynchronized `rpc_endpoints_` | **Confirmed blocker** | Invalidates Plan 09-13 truth 60 under an explicitly supported concurrent configuration path. |
| WR-01 — receipt bridge callback lifetime | **Confirmed warning** | `EthReceiptSourceBridge` installs a raw-`this` callback and has no teardown. This is a real UAF/leaked-watch risk, but it does not falsify canonical event identity or certificate lookup while the bridge is alive, so it does not reduce the must-have score. |

## Deferred Boundary

| Item | Addressed in | Evidence |
|---|---|---|
| Prevent concurrent competing certificates formed from empty distributed state | Phase 10 | Durable one-signature-per-slot vote journal/lock; Phase 9 research explicitly says local CRDT preflight is not distributed CAS. |

## Behavioral Verification

| Check | Result |
|---|---|
| Independent current-HEAD run: `consensus_slot_key_test` | PASS, 12/12 |
| Independent current-HEAD run: `consensus_certificate_store_test` | PASS, 23/23 |
| Independent current-HEAD run: `certificate_compatibility_test` | PASS, 17/17 |
| Independent current-HEAD run: `bridge_event_identity_test` | PASS, 19/19 |
| Independent current-HEAD run: `public_chain_mint_validation_test` | PASS, 12/12 |
| Independent current-HEAD run: `transaction_manager_pending_lifecycle_test` | PASS, 11/11 |
| Independent current-HEAD run: `bridge_e2e_chainlist_test` | PASS, 12/12 |
| Independent current-HEAD run: four Phase 09 CRDT mixed/shutdown tests | PASS, 4/4 |
| Post-merge full CMake build | PASS |
| Post-merge eight affected CTest suites together | PASS |
| Mixed CRDT repeat run | PASS, 20/20 |
| Complete `crdt_test` | PASS, 27/27 |
| `bridge_anvil_catchup_e2e_test` runtime | ENVIRONMENT SKIP: all ports 18545-18644 were occupied; cases skipped before code assertions |

No phase probes are declared or present, so probe execution is not applicable.

## Anti-Patterns and Disconfirmation Pass

- No `TBD`, `FIXME`, or `XXX` markers exist in the 40 reviewed phase files.
- Partial truth found: Plan 09-10's typed outcome is structurally correct, but its durable proof source is published too early.
- Misleading green-test boundary found: the worker shutdown test retains an external owner and therefore does not test the dangerous ownership transition.
- Uncovered error path found: endpoint writer/reader concurrency has no deterministic regression and no stable snapshot.
- The Anvil environment skip does not establish or refute these source-level blockers.

## Human Verification

No human action is needed to establish the `gaps_found` verdict. After the code gaps are closed, rerun `bridge_anvil_catchup_e2e_test` in an environment with a free port; the current port-exhaustion skip is environment evidence, not a code failure.

## Gaps Summary

Three concerns block completion:

1. Make mint effect application idempotent/transactional before CONFIRMED and executed-burn completion become durable.
2. Make CRDT shutdown lifetime-safe when the final owner disappears on an internal worker.
3. Publish or lock a stable RPC endpoint snapshot for each complete vote/validation decision.

The receipt-source bridge teardown issue remains a confirmed warning and should be fixed or explicitly accepted.

---

_Verified: 2026-07-24T16:23:12Z_
_Verifier: Codex (gsd-verifier)_
