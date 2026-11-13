---
title: MultiAccountTest.NodeConsensusTest registry catch-up timeout — pre-existing CI flake
area: testing
priority: P2
date: 2026-10-22
source: CI runs 39488390403 + 39794245061 (PR #398); debugger session ci-nodeconsensustest-registry-timeout-pr398
resolves_phase: null
---

"node_client validator registry not caught up (30000ms)" — the wait after
transfer2 in MultiAccountTest.NodeConsensusTest times out intermittently on
CI. NOT a PR #398 regression: reproduced at PR #398's exact merge-base
(688fcbb24, develop run 39291991120, OSX Debug) with zero PR changes; three
platforms over 9 days (10-13 aarch64, 10-14/10-15/10-19 macOS, 10-20+ x64);
the 17-03 relaxation commit is not even in PR #398's branch (phase-17/18
line only) and runtime logs show rung 1 answering every resolution.

Mechanism (from debug session): two competing registry-batch certificates at
the same slot (each node builds its batch subject from locally observed
pending slots, ValidatorRegistry.cpp:1087-1093); the losing cert must reach
every node via CRDT for FilterCertificate's deterministic tie-break to
converge — observed ~61s convergence under certificate-ingress load vs the
wait's 30s budget. Debug-config run fails the same wait with no conflict at
all (plain slow delta propagation under load).

Fix options: (a) raise wait_client_registry_caught_up budget to >=90s
(consistent with the file's own kFailFastBudget and its 61s observation);
(b) fix the race (single-proposer batch submission, or fast-path propagation
of the losing certificate). Regression test: after two competing certs land
at /cert/registry-batch:<cid>:<epoch>, all nodes' GetRegistryCid() converge
to the same winner within the raised budget.

Full evidence: .planning/debug/ci-nodeconsensustest-registry-timeout-pr398.md
