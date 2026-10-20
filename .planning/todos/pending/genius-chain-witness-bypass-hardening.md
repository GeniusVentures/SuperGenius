---
title: GENIUS_CHAIN_ID witness bypass is unconditional in released code
area: security
priority: P1
date: 2026-10-20
source: live node-example testing on staging net (owner + session analysis)
resolves_phase: null
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

Recommended fix: net-gate or config-gate the bypass; add an explicit
native-mint authorization policy; close the empty-chain_id sibling;
defense should be by policy, not by storage topology.
