# Phase 18 planning input — live grow-then-prune demo (owner expectation, 2026-10-20)

The owner wants to SEE the prune work live: history accumulate, transactions
become eligible, physical erasure run, and eventually disk shrink (shrink proof
itself is Phase 20's measured-bytes gate). Blocked on staging by the absence of
spendable tokens (unbacked dev mints verified blocked at three independent
gates). Raise as a gray area in /gsd-discuss-phase 18:

- Dev-history seeding command for node_example (test-sanctioned writes through
  the CRDT/UTXO paths the ctest fixtures use — WritePrunedEntryForTest
  precedent): construct mint+spend history on a dev net so Phase 18's first
  real prune is observable live, and operators get a demo path.
- Related folded todo: mintfunds-leaks-burn-utxo-on-consensus-reject
  (resolves_phase 18).
