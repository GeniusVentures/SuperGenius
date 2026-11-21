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
- Resolved in plan 06: owning `GetCertificateBySlot` preserves NOT_FOUND and
  storage errors, maps rejected validation to invalid_argument, and maps
  Pending/Stalled authority to resource_unavailable_try_again. The nonce
  recovery lookup reuses that same authority. The release resolver consumes
  the classified result directly; its temporary raw-record probe was removed.
  Corrupted votes/quorum/registry bindings reject; missing/deferred evidence
  remains retryable. No private authorization policy was copied into accounts.
- Resolved in plan 04 and retained in plan 06's complete fast-target run: full
  V2 `HandleNonceConsensusSubject` approve, invalid-payment reject and
  missing-hold/certificate pending cases run through tag-10 dispatch and the
  manager parser. Historical Transfer replay still requires private exact
  canonical evidence; no fresh backdated Transfer escrow exception exists.
