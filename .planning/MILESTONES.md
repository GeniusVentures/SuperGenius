# Milestones

## v3.0 Canonical Burn Finality Rebuild (Shipped: 2026-09-03)

**Phases completed:** 5 phases (8-12), 34 plans, 48 tasks

**Stats:** 186 files changed, +72,648 / −1,029 lines · 21/21 requirements validated · 290 commits · 14 days (2026-08-20 → 2026-09-03) · git range `8d66d670a → 94b54905e`

**Key accomplishments:**

- Canonical burn-slot identity shared by every competing mint proposal, cryptographically bound to the exact winning proposal (Phase 8)
- Durable one-vote-per-slot lock persisted to RocksDB before broadcast, cleared only on matching durable finality (Phase 9)
- Deterministic verifiable publisher authority: persistence-before-advertisement, lowest-SHA-256 convergence for contested immutable records, safe failover with no bridge-specific finality side channel (Phase 10)
- Convergent certificate consumption with the CRDT work journal as the sole retry boundary; certificate-first Mint recovery with tightly validated embedded fallback; Mint V2 held VERIFYING until idempotent effects and bridge marker both persist (Phase 11)
- Real-socket four-peer production-path fault proof: one canonical slot, one authoritative certificate, one exact mint effect through contention, propagation disorder, publisher loss, and restart — exact-once held in every run ever recorded (Phase 12, TEST-01..06)
- Post-restart certificate recovery made reliable: surviving-replica retention before publisher restart + mesh-readiness-gated re-advertisement (RestartAtVote from ~50%/run to 8 consecutive full-suite greens)
- Real production defects found and fixed under evidence discipline: asio teardown-order SIGSEGV (propagated across all proof artifacts), stale test-fixture RocksDB reuse (run-unique + reap), SameBurn check-then-act wait race, silent no-quorum rejection made visible
- Publisher-observer meta-test apparatus removed by developer directive at close (801 lines) — the regression suite is the six finality scenarios, three-consecutive-serial-pass verified with zero crash reports

**Known deferred items at close:** 13 (see STATE.md Deferred Items — stale v1.0-era items, June quick-tasks from the TokenId subsystem, and debug sessions mooted by the apparatus removal). Plus standing deferred: thirdparty StopImpl hardening, MintRecoveryDiagnostics UAF, WR-02 notify-under-paired-mutex, CRDT equal-priority overwrite guard.

---

## v1.0 GeniusNode Construction Refactor (Shipped: 2026-07-03)

**Phases completed:** 3 phases, 5 plans, 0 tasks

**Stats:** 39 files changed, +2956 / −341 lines · 12/12 v1 requirements validated · git range `2585e6a9 → HEAD`

**Key accomplishments:**

- Config-driven network settings: `auto_dht` + `port_seed` (renamed from `base_port`) read from `network_config.json` in `InitNetwork()` with config-wins precedence, safe defaults, and `pubsub_port` > `port_seed` priority Doxygen (Phase 1 — CFG-01, CFG-04)
- `NodeType` enum (Full/Light/Archive) + case-insensitive `NodeTypeFromString()` + `node_type` read in `LoadSgnsConfig()`; `is_full_node_` derived in a reordered private ctor that creates the account via `std::visit` **after** `LoadSgnsConfig` resolves the role (the init-order hinge fix) (Phase 2 — CFG-02, CFG-03)
- Canonical `New(dev_config, AccountSource)` variant factory (`AccountSource = std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>`, `FromPublicKey` promoted public); `nullptr`-on-failure preserved; old factories retained through Phase 2 then deleted (Phase 2 — INTF-01/02/03; Phase 3 — INTF-04)
- All ~25 `NewFromPrivateKey`/old-`New` call sites across 14 files migrated to `New(dev_config, FromPrivateKey{...})`; shared `WriteNetworkConfig`/`WriteSgnsConfig` helpers (truncate, create-dir, validate `node_type`); old factories + old private ctor deleted — `New(dev_config, AccountSource)` is the sole entry point (Phase 3 — MIG-01/02/03/04)
- Full build + CTest green; no behavior change for default or pre-existing config files (deployed configs without the new keys keep working via defaults)

**Known deferred items at close:** 0 (open-artifact audit clear). Future-milestone items noted in REQUIREMENTS v2: `NodeType` downstream propagation (PROP-01), distinct Archive-vs-Full behavior (PROP-02), `pubsub_port` numeric cleanup (HARD-01), config schema versioning (HARD-02).

---
