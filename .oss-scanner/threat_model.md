# Anthropic OSS Scanner threat model — SuperGenius

## System boundary

SuperGenius is the C++ GNUS.ai distributed-node implementation. It includes public submodules for cryptography, proof systems, node processing, peer communication, and EVM relay code. The scanner must consider the pinned contents of `GeniusKDF`, `ProofSystem`, `SGProcessingManager`, `evmrelay`, and other public gitlinks, not just files physically committed to the parent tree.

## Untrusted inputs and invariants

- Any remote peer, network stream, pubsub message, node announcement, Merkle proof, ledger/CRDT record, RPC response and external asset is hostile unless authenticated and authorized.
- Malformed messages or serialized objects must not cause buffer overflow, out-of-bounds access, type confusion, use-after-free, or unbounded allocation.
- Peer identity, attestations, consensus/quorum state and cryptographic proofs must not permit forgery, replay, double counting, chain-ID confusion, or unauthorized state changes.
- Wallet key material and local secure storage must not leak, accept weak recovery methods, or cross user or process boundaries without authorization.
- RPC, bridge, and token-settlement paths must not invent balances, bypass payouts, or accept signatures under the wrong domain.
- Processing tasks must respect capability limits, memory/compute budgets, identity, cancellation, and private-data boundaries. A malicious job/asset must not escape its assigned scope.
- Node failure, delayed messages, malicious clocks, network partitions, duplicate jobs, and replay must preserve safety even if availability drops.

## Severity

**Critical:** remote code execution, unauthorized token movement, signing/key compromise, or full attestor/quorum takeover reachable by an untrusted peer.

**High:** reliable remote memory corruption, cross-node privilege escalation, invalid financial state, message-signature bypass, or persistent private-data disclosure.

**Medium:** bounded denial of service, peer starvation, economic griefing or limited information disclosure. **Low:** hardening suggestions without a reachable attack.

For each finding, identify exact source and pinned submodule, attack surface, required malicious input, successful reproducer and expected impact. Suggest a narrow patch and a regression test with a failing and a passing case.

## Offline reproductions

The image contains source, linked artifacts and tests at `/src/build/Linux/Release/x86_64`:

```bash
cd /src
ctest --test-dir build/Linux/Release/x86_64 --output-on-failure --timeout 180 -R '^securecrdt_interface_test$'
```

Other selected CTests can run offline, but some node-booting tests need a temporary D-Bus/gnome-keyring session, and GPU/Vulkan tests may require host capabilities not supplied by an isolated scanner. Do not mistake expected skips for successful security coverage.

## Exclusions

Do not use real private keys, live GNUS/token chain transfers, production peers, or external APIs. Do not run tests requiring public CoinGecko or remote chain RPC access in offline mode. Only report third-party bugs when a reachable SuperGenius path makes them exploitable. Findings are confidential to `admin@gnus.ai`.
