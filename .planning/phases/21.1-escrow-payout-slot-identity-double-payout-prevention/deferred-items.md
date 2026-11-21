# Deferred execution findings

## Plan 21.1-01

- The existing `.clang-tidy` naming configuration uses `snake_case` for four
  identifier-naming options. Installed LLVM 22.1.8 reports those values as
  invalid. This configuration predates the payment extraction and is outside
  its scope; review the intended naming options separately. Changed-line
  checks still ran, and all Required findings introduced by this plan were
  cleared before commit.

## Plan 21.1-02

- `base::Hash256::fromReadableString` at `src/base/blob.hpp:103` accepts an odd
  65-character input through its integer-divided length check, then can write
  past its 32-byte blob; its `std::stoul` also throws on nonhex pairs and accepts
  partial pairs. New release decoders use the existing checked `fromHex`
  instead. Repair/audit of the pre-existing helper's other callers is outside
  this plan's ten-file codec ownership and remains deferred.

## Plan 21.1-03

- `GeniusInputValidator.cpp:94` retains the existing no-op token-registration
  hook. V2 rejects malformed output TokenID values and binds the consumed token
  to the actual certified hold, while valid result-output tokens retain the
  builder's existing policy. No token registry or unknown-token rejection is
  claimed by this plan.
- The owning `GetCertificateBySlot` API collapses authorization deferral and
  rejection into the same error; its classified validation helpers are private.
  The release resolver approves only through that API, rejects demonstrable
  parse/key/hash/exact-transaction contradictions, and conservatively pends
  other failed lookups, including a corrupted vote. A future owner-provided
  classified lookup may refine liveness; raw record presence never authorizes.
- Plan 04 must run the complete V2 `HandleNonceConsensusSubject` and ledger
  ingress cases after adding tag-10 embedded dispatch and manager parser
  registration. Plan 03 exercises the actual local type-rule and witness
  gates, plus full ordinary-Transfer ingress (approve and missing-certificate
  pending). Carry forward V2 approve, invalid-payment reject and missing-hold /
  missing-certificate pending cases. Do not restore a fresh backdated Transfer
  escrow exception to make historical replay pass.
