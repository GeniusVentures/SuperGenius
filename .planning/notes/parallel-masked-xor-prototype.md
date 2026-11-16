# Parallel masked-XOR sampled verification prototype

**Branch:** `feature/parallel-masked-xor-verifier-tests`. **Status:** experimental, not wired to settlement, payouts, or consensus.

## Intent

Retain the N+1 SuperGenius processing design. The requestor privately maps overlapping physical blocks to unrelated assignment IDs, distinct local indices, different worker nodes, and different salted nonces. Every node computes independently; no commit-then-challenge barrier is introduced.

1. **Requestor:** draw a cryptographically random 32-byte nonce and an independent 16-byte salt *per assignment* (use OS CSPRNG, not `std::mt19937` or the existing float `Task.random_seed`). Derive `M = SHA256(domain || length-framed jobID, assignmentID, localIndex, salt, nonce)`. Send only the prehashed mask M with each assignment. Keep the overlap mapping private until verification.
2. **Worker:** hash the canonical output bytes as `H = SHA256(domain || length-framed output)`; submit `C = H XOR M` with assignment and node metadata. The caller must verify its node signature; this prototype has no signing or transport.
3. **Requestor:** before seeing results, bind the full overlap mapping and `N = XOR(maskA, maskB)` to an authenticated, immutable job commitment in the ledger. A caller must enforce this; the prototype only checks that the supplied N matches the supplied plan.
4. **Final check:** accumulate selected contributions with XOR, then compute `R = XOR(all selected C) XOR N`. For correctly replicated blocks `R = 0`. The verifier also checks every matched pair separately, since two equal XOR errors in different pairs otherwise cancel. These checks happen after workers finish, without delaying any worker.
5. **Overall computation hash:** `OverallComputationHash` produces an *ordered* digest of separately supplied block-result digests, for integrity/identification. It is not itself proof of correct execution.

## Security scope

This is a **probabilistic sampled consistency check**, not a SNARK/STARK, proof of all output blocks, proof of input privacy, or public proof of honest hardware execution. The field `node_id` must be authenticated against the signed work assignment outside this module. A matching pair can both submit the **same wrong output** and pass; tests document that limitation. A single aggregate XOR without pair checks permits compensating mismatches; tests reject that case. Revealing prehashed nonce masks does not hide their corresponding output hashes from the recipient; hiding the verifier requires that the overlap mapping not be disclosed and network and source-data metadata not reveal it.

Independent executions may legitimately produce different byte-level outputs due to floating-point variation. Canonicalize outputs or apply the existing bounded tolerance logic before using this exact-hash path on such workloads. No Pallas commitments or polynomial commitments are needed for the prototype's arithmetic; Pallas does not by itself prove computation correctness.

**Do not enable for live escrow/consensus** until plan precommitment, signed assignments/results, replay protection across epochs, safe nonce generation, sampler design, collusion analysis, and cross-device canonicalization are implemented and tested.

## Tests

The CMake target `masked_xor_verification_test` runs unit cases for nonce/job/index/salt binding, matching and mismatched outputs, forged masks, missing/duplicate/extra results, node/index substitution, cross-job replay, invalid plans, compensating XOR errors, and the collusion blind spot. Functional cases run 12 independent block pairs on asynchronous C++ worker threads in reverse arrival order, then test corruption, dropped results and replay. All processing starts independently; there is no serial verification gate.

Source: `src/processing/masked_xor_verification.hpp/.cpp`. Tests: `test/src/processing/masked_xor_verification_{test,functional_test}.cpp`. C++17 and the existing SHA-256 implementation.
