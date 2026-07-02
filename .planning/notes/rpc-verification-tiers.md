---
title: RPC Verification Tiers — Architecture Decisions
date: 2026-06-03
context: Explored during PR #298 merge follow-up planning
---

## RPC Verification Model

### Tier 1 — Per-Node RPC Confirmation (this branch)

Each node independently queries its configured RPC endpoints to verify a burn transaction:

- **Not weighted consensus** — simple majority: ≥2 of 3 endpoints must return the same receipt data
- Disagreement is **not flagged** — a lagging endpoint that hasn't indexed the tx yet is expected behavior
- All 3 endpoints agree → good. 2 of 3 agree → good. 1 of 3 agree → fail. 0 of 3 → fail (fail-closed)

### Tier 2 — Network Voting Classes (separate phase, follow-up)

Once a node confirms the burn via Tier 1, it submits a vote to the consensus round:

- **Direct RPC nodes (50% weight):** Nodes with API-key-based direct RPC connections. Require ≥51% of this cohort to approve. Reputation score identifies full nodes.
- **Public-only RPC nodes (25% weight):** Nodes using only public RPC endpoints. Require ≥51% of this cohort to approve. Reputation-based.
- Final approval needs both cohorts to meet their thresholds.

## Mock RPC Transport

In-process mock for testing Tier 1, configurable per-node:

- **Data variance:** Different receipt logs, tx statuses, addresses per endpoint
- **Behavioral variance:** Some endpoints return success, others error/timeout
- **Stateful variance:** Mock remembers prior calls (e.g., second call fails)
- **Multi-chain:** Works across all chains including testnet chains
- **Per-node config:** Each node loads its own mock endpoint set from a local config file (test/dev only)

## Scope Split

| Branch | Scope |
|--------|-------|
| Feature branch (this) | BridgeRelayer::Start(), InitializeRpcEndpoints(), bridge_contract_address, Mock RPC transport, Tier 1 majority verification |
| Tier 2 follow-up | Network voting weight classes, direct vs public node reputation, consensus quorum rules |
