# SuperGenius Submodule Map

Maps nested submodules inside `SuperGenius/`. Planning artifacts live in `SuperGenius/.planning/`.

---

## Nested Submodules

| Submodule Path | Remote | Notes |
|---|---|---|
| `GeniusKDF/` | GeniusVentures/GeniusKDF | Key derivation function |
| `GeniusKDF/build/` | GeniusVentures/cmaketemplate | Build system template |
| `ProofSystem/` | GeniusVentures/ProofSystem | ZK proof system |
| `ProofSystem/SGProofCircuits/` | GeniusVentures/SGProofCircuits | Proof circuit definitions |
| `ProofSystem/SGProofCircuits/build/` | GeniusVentures/cmaketemplate | Build system template |
| `ProofSystem/build/` | GeniusVentures/cmaketemplate | Build system template |
| `SGProcessingManager/` | GeniusVentures/SGProcessingManager | AI/ML job processing manager |
| `SGProcessingManager/build/` | GeniusVentures/cmaketemplate | Build system template |
| `evmrelay/` | GeniusVentures/evmrelay | EVM bridge relay |
| `evmrelay/build/` | GeniusVentures/cmaketemplate | Build system template |
| `gRPCForSuperGenius/` | GeniusVentures/gRPCForSuperGenius | gRPC proto definitions |
| `docs/` | GeniusVentures/sg-docs | SuperGenius documentation |

## Planning Directory Ownership

All nested submodules use `SuperGenius/.planning/` for workstream tracking. If a nested submodule needs independent workstreams, initialize its own `.planning/` with `/gsd:new-project`.

---

*Generated: 2026-07-06*
