# Phase 12: ValidatorRegistry Migration - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-27
**Phase:** 12-ValidatorRegistry Migration
**Areas discussed:** Weighted quorum vs MultiSig's unweighted model, Phase 11 regression risk posture, final scope call

---

## Weighted quorum vs MultiSig's unweighted model

Scouting confirmed `ValidatorRegistry` uses weighted quorum (`QuorumThreshold`/`IsQuorum`/`EvaluateSlotQuorum`, weighted certificate/vote finalization), while `multisig::EvaluateQuorum` and `ISignedCRDTData` are strictly unweighted with no signer-list hook. Closing this gap for a full `ISignedCRDTData` migration would require extending already-shipped Phase 8/9 interfaces.

| Option | Description | Selected |
|--------|-------------|----------|
| Extend ISignedCRDTData with signer-aware Verify | Add a signer-aware variant so SecureCrdt::ReadIfQuorum can pass signers through; ValidatorRegistry does its own weight math there | |
| Add weight support directly to MultiSig::EvaluateQuorum | Give EvaluateQuorum an optional weight map/threshold; reusable elsewhere but changes shipped, tested Phase 8 code | |
| Don't route weighted logic through SecureCrdt at all | Scope to genesis creation only (trivial single-signer -> N-of-M=1 mapping); leave certificate/vote pipeline untouched | |

**User's actual answer (reframed the question):** "Validator Registry shouldn't use SignedCRDTData, it just needs to use the MultiSig. As far as storing it already has its own scheme that relies on consensus. The re-use here would be applicable only to the multi signature. But if that is too much of a change I think we can drop it entirely." — This rejected all three drafted options in favor of a narrower framing: reuse `MultiSig`'s signature-verification primitive only, keep `ValidatorRegistry`'s own CRDT storage/consensus scheme entirely untouched, with "drop the phase" as an explicit fallback if that reuse turned out to be non-trivial.

---

## Phase 11 regression risk posture

| Option | Description | Selected |
|--------|-------------|----------|
| Proceed with migration, require N clean multi_account_test reruns as exit gate | Don't block on root-causing Phase 11's open regression first; verification bar is 5-10 clean reruns | ✓ |
| Root-cause the Phase 11 regression as a prerequisite before starting Phase 12 | Dedicated investigation/profiling first, then start Phase 12 on a clean baseline | |

**User's choice:** Proceed with migration, require N clean multi_account_test reruns as an explicit exit gate (Recommended)

Note: this decision was made before the scope narrowed (see above/below) to exclude any new CRDT/SecureCrdt surface — under the final narrowed scope, this posture is lower-stakes than originally framed, since D-03/D-04 in CONTEXT.md establish that no new CRDT filter/callback registration is introduced at all.

---

## Final scope call

Given the user's reframing above, Claude proposed a concrete narrow scope: swap exactly 2 signature-verification call sites (`StoreGenesisRegistry`'s signing counterpart, `VerifyUpdate`'s genesis-path check) from `GeniusAccount::VerifySignature` to `multisig::VerifyPayloadSignature`, plus one CMake link — no architecture change, no new CRDT surface.

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, proceed with Phase 12 on this narrow scope | Small, low-risk, delivers on the original signature-reuse goal | ✓ |
| No, drop Phase 12 — close out the milestone at Phase 11 | Even this narrow scope isn't worth it | |

**User's choice:** Yes, proceed with Phase 12 on this narrow scope (Recommended)

---

*Phase: 12-ValidatorRegistry Migration*
*Discussion completed: 2026-07-27*
