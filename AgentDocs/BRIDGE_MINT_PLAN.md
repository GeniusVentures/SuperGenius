# Bridge Validation Plan — Option A (Direct RPC Verification, No Burn Consensus)

## Overview
This plan describes the implementation of a **Burn → Mint bridge validation flow** using **direct EVM RPC verification** without a separate consensus round for Burn finalization.  
The design leverages Ethereum finality (via confirmations or finalized block tag) and SuperGenius consensus for Mint validation.

---

## 1. Core Principles

- **Single consensus round:** Only the Mint proposal is subject to consensus.
- **EVM-side finality check:** Burns are trusted only after confirmed finalization.
- **Deterministic verification:** Validators independently verify the same Burn hash via RPC.
- **UTXO-based uniqueness:** Each Burn hash can be used exactly once for Minting.

---

## 2. Data Model

### Burn Event (EVM)
Emitted by the Burn contract:
```solidity
event Burn(address indexed sender, uint256 amount, bytes32 recipient, uint256 nonce);
```

### Mint Proposal (SuperGenius)
```
struct BridgeMintProposal {
    bytes32 burn_hash;       // Hash of the Burn transaction
    address recipient;       // Destination address on SuperGenius
    uint256 amount;          // Amount to mint
    uint64 evm_chain_id;     // Source chain ID
}
```

### Local Cache
Validators and relayers maintain:
```
burn_cache[burn_hash] = {
    block_number,
    finalized: bool,
    used: bool
}
```

---

## 3. Flow Summary

### Step 1 — Burn Detection
- Relayer monitors EVM via WebSocket or polling.
- On detecting a Burn event:
    - Extract `burn_hash`, `recipient`, `amount`.
    - Record `block_number`.

### Step 2 — Wait for Finalization
- Wait until:
    - `current_block - burn_block >= FINALITY_DEPTH`  
      **OR**
    - `burn_block <= rpc.getBlock("finalized").number`
- Mark `burn_cache[burn_hash].finalized = true`.

### Step 3 — Submit Mint Proposal
- Once finalized, relayer submits `BridgeMintProposal` to SuperGenius consensus.

### Step 4 — Consensus Validation Callback
Each validator executes:
```cpp
Outcome<void> validateMintProposal(const BridgeMintProposal& proposal) {
    auto burn = rpc.getTransactionReceipt(proposal.burn_hash);
    if (!burn.success || !burn.is_finalized()) {
        return Error::InvalidBurn;
    }
    if (burn.to != BurnContractAddress) {
        return Error::InvalidTarget;
    }
    if (burn.amount != proposal.amount) {
        return Error::AmountMismatch;
    }
    if (burn_cache[proposal.burn_hash].used) {
        return Error::DoubleMintAttempt;
    }
    return outcome::success();
}
```

### Step 5 — Consensus Aggregation
- SuperGenius consensus aggregates validator votes.
- If ≥⅔ votes are valid → Mint approved.

### Step 6 — Mint Execution
- Bridge module mints tokens to recipient.
- Marks `burn_cache[burn_hash].used = true` to prevent reuse.

---

## 4. Security Measures

| Risk | Mitigation |
|------|-------------|
| RPC manipulation | Validators use independent RPC endpoints and cross-verify responses. |
| EVM reorgs | Wait for block finalization before proposal submission. |
| Double minting | Burn hash marked as spent after successful Mint. |
| RPC divergence | Consensus rejects if validator RPC results differ. |
| Latency | Configurable FINALITY_DEPTH for chain-specific tuning. |

---

## 5. Implementation Notes

- **RPC abstraction:** Implement a `BurnVerifier` class with pluggable backends (RPC, future BridgeClaim).
- **Consensus subject:** Reuse existing `BridgeMintProposal` subject type.
- **Outcome-based error handling:** Use `Boost.Outcome` for all validation results.
- **Testing:** Simulate reorgs and RPC failures in unit tests to ensure deterministic outcomes.
- **Future-proofing:** Interface design allows replacing RPC verification with consensus-backed BridgeClaims later.

---

## 6. Example Parameters

| Parameter | Value | Description |
|------------|--------|-------------|
| `FINALITY_DEPTH` | 15 blocks | Wait depth for EVM finality |
| `RPC_TIMEOUT` | 5s | Timeout for RPC calls |
| `RETRY_INTERVAL` | 12s | Polling interval for finalization checks |
| `MAX_RPC_SOURCES` | 3 | Number of independent RPC endpoints per validator |

---

## 7. Summary

Option A provides a **lean, deterministic, and secure bridge validation path** relying on:
- EVM finality for Burn confirmation,
- SuperGenius consensus for Mint finalization,
- UTXO-style uniqueness for replay protection.

It avoids unnecessary consensus duplication while maintaining safety through independent RPC verification and finality waiting.
