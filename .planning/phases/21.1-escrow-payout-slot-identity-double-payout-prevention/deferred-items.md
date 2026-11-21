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
