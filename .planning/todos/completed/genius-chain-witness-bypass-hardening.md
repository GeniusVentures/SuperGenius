---
title: GENIUS_CHAIN_ID witness bypass is unconditional in released code
area: security
priority: P1
date: 2026-10-20
source: live node-example testing on staging net (owner + session analysis)
resolves_phase: 18
resolved: 2026-10-21
resolution: resolved-by-design
---

The chain_id == "supergenius" (and empty chain_id) bypass in
PublicChainInputValidator::GatherVerificationEvidence (PublicChainInputValidator.cpp:326-334)
was added to enable ctest flows, but has NO net-id gating, no test-build flag,
and no authorized-minter policy — it ships in the released node binary and
applies to every net.

Current exposure (verified in source 2026-10-20): an unbacked native mint is
blocked from certification on honest-quorum nets ONLY by the incidental fact
that CheckTransactionTypeRules -> ValidateUTXOParameters -> VerifyParameters
rejects unknown outpoints and /utxo/ state is local-only (never replicated).
The witness layer itself accepts any signed mint declaring the local chain id.

Risks:
- Attacker-controlled quorum or single-node nets: unbacked certification with
  no code modification (magic chain id).
- If /utxo ever becomes replicated or type-rules is loosened, the bypass
  silently becomes an unrestricted mint path.

RESOLUTION LOCKED (2026-10-21, 18-CONTEXT D-03): net-scoped dev-mint policy — the allowance becomes explicitly net-scoped (staging + local dev) with validators refusing certification elsewhere. Supersedes the generic recommendation below.

Recommended fix: net-gate or config-gate the bypass; add an explicit
native-mint authorization policy; close the empty-chain_id sibling;
defense should be by policy, not by storage topology.

Severity refinement (2026-10-20, live testing): a THIRD gate sits in front —
SendTransactionItem/BuildUTXOWitness requires the producer transaction of every
mint-v2 input on the genius chain (observed: "Missing producer transaction for
input" on Light, send aborted pre-broadcast). Fabricated burn refs cannot pass
it, so the bypass alone does not yield a certifiable unbacked mint even
single-node. Exposure remains: quorum-controlled nets, and the incidental
nature of gates 2/4 (defense by storage topology / witness-builder behavior,
not by policy).

## Resolution (Phase 18, plan 18-02 — resolved-by-design)

The D-03 net-scoped dev-mint policy replaces the global bypass (commits
9f802a1f1, f8d65d1c on gsd/phase-18-txpruner-service-first-real-prune):

- **Both bypass legs net-scoped** (`PublicChainInputValidator::GatherVerificationEvidence`,
  via the `LocalBypassEvidence` helper): the empty-source-reference leg and the
  empty/`"supergenius"`-chain-id leg each consult `DevMintBurnlessPermitted()`;
  on non-designated nets both return default-invalid evidence (the
  no-endpoints reject shape) instead of certifying. The unconditional bypass
  no longer exists in released code paths on test/main nets.
- **`DevMintPolicy.hpp`** (beside the sgns_version net constants): the
  designated set `{staging 333, dev 144}` is `inline constexpr` — config can
  never widen it; the predicate reads ONLY `version::GetNetworkID()` (the
  validator's own net), never a tx/proposal field.
- **`DevMintInputValidator`** registered for the literal chain id `"dev"`
  (`IInputValidator` registry, `kDevMintValidatorRegistered` anchor beside the
  genius anchor): `kValid` iff the validator's own net is designated — the
  explicit allowance AND the refusal; full consensus (proposal, quorum,
  certificate) still applies on the designated nets.
- **Proofs** (`test/src/account/dev_mint_policy_test.cpp`, 11 tests): both
  bypass legs × both non-designated nets refuse (the empty-chain-id leg
  explicitly asserted); identical witness bytes refused on 963/369, permitted
  on 333/144; a `"dev"`-chain burn-less mint certifies through REAL quorum on
  net 333 with its produced UTXO landing on every peer; the identical subject
  and the empty-chain-id mint never certify on net 963.

The recommended fix below ("net-gate or config-gate the bypass") is superseded
by the locked design: constant set, not config (config would weaken validator
refusal to a per-node opt-in). Defense is now by policy, exactly as the todo
asked.
