# SuperGenius Consensus Voting Decentralization

## What This Is

A protocol change to SuperGenius consensus that embeds the full serialized transaction in `NonceSubject` objects, so any validator can validate and vote on proposals without needing the transaction in their local CRDT store. Currently only the Genesis node, full nodes, and the transaction's destination peer can vote — this fix makes the voting pool truly distributed.

## Core Value

Any active validator can independently validate and vote on any transaction proposal, regardless of whether they received the transaction data out-of-band — consensus becomes genuinely decentralized.

## Requirements

### Validated

(None yet — ship to validate)

### Active

- [ ] Full serialized transaction bytes embedded in `NonceSubject` protobuf message
- [ ] `HandleNonceConsensusSubject` deserializes transaction from the subject instead of looking up from CRDT (`GetTransactionPath(tx_hash)` → check that currently returns `Check::Pending`)
- [ ] All existing validation checks (well-formed, authorization, timestamp, replay protection, type rules, witness, input conflict) work from embedded data
- [ ] Non-destination peers can reach `Check::Approve` and cast votes
- [ ] Certificate creation and quorum logic unchanged (only the validation path changes)
- [ ] Existing tests pass; new tests validate non-CRDT peer voting

### Out of Scope

- TaskResultSubject changes — not yet implemented, no voting problem exists
- RegistryBatchSubject changes — validators already subscribe to registry CRDT, no voting gap
- Backward compatibility — clean protocol break is acceptable per project decision
- Changing the consensus quorum or certificate mechanics — voting/validation only

## Context

SuperGenius uses a CRDT-replicated GlobalDB for transaction persistence and a PubSub-based consensus system for proposal/vote/certificate exchange. The `NonceSubject` currently carries a `tx_hash` reference; peers look up `/tx/{tx_hash}` in their local CRDT to get the full transaction for validation. Only peers that received the transaction (Genesis, full nodes, destination) have the data. Other validators receive the proposal via PubSub but return `Check::Pending` because they lack the transaction.

The fix adds a `bytes transaction_data` field to the `NonceSubject` protobuf, serialized at proposal creation time, and changes the handler in `TransactionManager::HandleNonceConsensusSubject` to deserialize from the subject payload rather than CRDT. The UTXO commitment and witness are already in `NonceSubject` — embedding the transaction bytes provides the complete binding proof (validator can reconstruct the commitment from the tx UTXO params and compare).

## Constraints

- **Protocol**: Clean break — no backward compatibility with old peers
- **Scope**: `src/blockchain/impl/proto/Consensus.proto`, `src/blockchain/Consensus.cpp`, `src/account/TransactionManager.cpp`, test files
- **Security**: The binding between transaction UTXO params and the subject's utxo_commitment must be verified by validators (reconstruct + compare); can't rely on commit/witness alone without tx params

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Embed full tx (not a subset) | UTXO params are needed to verify binding with commitment; sending DAG struct only is insecure | — Pending |
| NonceSubject only | Other subject types don't have this voting gap yet | — Pending |
| Clean break, no backward compat | Simpler implementation; protocol version negotiation adds complexity without benefit at this stage | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-05-27 after initialization*
