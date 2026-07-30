# Phase 12: Consensus Race and Compatibility Verification - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-30
**Phase:** 12-consensus-race-and-compatibility-verification
**Areas discussed:** Todo routing, race proof evidence, deterministic interleavings, test organization, compatibility gate

---

## Todo Routing

| Topic | Option | Selected |
|-------|--------|----------|
| Existing 11-node race timeout | Fold into Phase 12 | ✓ |
| Existing 11-node race timeout | Keep separate | |
| Startup wiring and mock RPC | Keep separate | ✓ |
| Startup wiring and mock RPC | Fold into Phase 12 | |

**User's choice:** Fold the 11-node race failure; keep startup wiring and mock RPC separate.
**Notes:** Phase 12 owns proof and diagnosis of the race, not new RPC infrastructure.

---

## Race Proof Evidence

| Question | Option | Selected |
|----------|--------|----------|
| What proves all 11 competed? | Each observes the burn, submits a proposal for the same slot, then converges | ✓ |
| | Observation/join is enough even if some never publish | |
| | Readiness and final convergence only | |
| How is non-equivocation proven? | Record each validator's usable published signature and allow at most one per slot | ✓ |
| | Infer it from one certificate and balance | |
| | Require all 11 signatures on the winner | |
| What is one confirmed mint? | Same winning hash everywhere, one confirmation/application, losers unconfirmed | ✓ |
| | Balance increase only | |
| | Different local confirmed hashes allowed | |
| What failure evidence is required? | Bounded structured per-node summary | ✓ |
| | Assertion plus manual full-log inspection | |
| | Persist all raw traffic and databases every run | |

**User's choice:** Selected the strongest direct evidence option for all four questions.
**Notes:** “Competing” is proposal participation in one canonical slot, not multiple locally confirmed mints.

---

## Deterministic Interleavings

| Question | Option | Selected |
|----------|--------|----------|
| Force certificate-before-application gap | Friend-only pause after finality and before application/cleanup | ✓ |
| | Repeated thread scheduling | |
| | Mock consensus pipeline | |
| Model restart | Destroy/reconstruct manager over the same durable store | ✓ |
| | Restore the same live manager | |
| | Kill/relaunch the entire external cluster | |
| Control candidate ordering | Test-controlled clock/deadline trigger | ✓ |
| | Short real-time sleeps | |
| | Infer ordering from network logs | |
| Assert while application is paused | Lookup winner, block competitor, idempotent duplicate, reject/record conflict | ✓ |
| | Assert no second certificate only | |
| | Wait for application before asserting | |

**User's choice:** Selected deterministic production-path controls and the complete finality contract.
**Notes:** The controls remain private test seams and do not replace the genuine network race.

---

## Test Organization

| Question | Option | Selected |
|----------|--------|----------|
| Scenario split | 11-node E2E race plus focused deterministic tests | ✓ |
| | Put everything in the 11-node executable | |
| | Focused tests only | |
| Focused test ownership | Existing subsystem targets plus one cross-component integration target | ✓ |
| | One monolithic Phase 12 source file | |
| | Put everything in the single-burn source | |
| Evidence collection | Private structured observers/accessors | ✓ |
| | Parse logs | |
| | Add public production diagnostic APIs | |
| Expensive race gating | Isolated long-running CTest target, mandatory for Phase 12 | ✓ |
| | Run in every default unit invocation | |
| | Optional manual test | |

**User's choice:** Selected the hybrid suite, subsystem ownership, private observation, and mandatory isolated race.
**Notes:** Focused tests provide deterministic causality; the network race proves integration reality.

---

## Compatibility Gate

| Question | Option | Selected |
|----------|--------|----------|
| Mandatory baseline | Explicit direct-consumer compatibility manifest | |
| | New Phase 12 tests only | |
| | Entire repository suite, including unrelated and external integrations | ✓ |
| Unavailable external prerequisites | Explicit reviewed skip recording the missing dependency | ✓ |
| | No skips under any condition | |
| | Silently omit unavailable tests | |
| Corruption diagnostics | Typed failure, unchanged authoritative state, diagnostic without exact wording | ✓ |
| | Exact log wording | |
| | Generic lookup failure only | |
| Duplicate/conflict delivery | Every externally reachable certificate ingress | ✓ |
| | Shared internal finalization only | |
| | Network/pubsub ingress only | |

**User's choice:** Required the entire repository suite, with accountable prerequisite skips, typed fail-closed assertions, and all external certificate ingress paths.
**Notes:** The full-suite choice deliberately makes Phase 12 completion broader than the direct TEST-01..06 target list.

---

## the agent's Discretion

- Private seam and observer names and exact placement.
- Diagnostic formatting and timeout values supported by measured behavior.
- Full-suite command/report structure and the name of the dedicated integration target.

## Deferred Ideas

- Bridge relayer startup wiring.
- `InitializeRpcEndpoints()` startup wiring.
- Configurable in-process mock RPC endpoints and majority-verification scenarios.
