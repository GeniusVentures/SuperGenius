# Pitfalls Research — v1.3 Security Remediation & Network Readiness

**Domain:** Adding 25 security-assessment remediations to a live C++17 blockchain/P2P system (SuperGenius + libp2p fork + GeniusSDK) with existing multi-node deployments, CRDT-replicated state, and a real-socket test suite — gated on pre-public-network exposure.
**Researched:** 2026-10-19
**Confidence:** HIGH for repo mechanics (every cited code path verified against current source this date); MEDIUM-HIGH for external patterns (official specs/docs + multiple independent sources; community-sourced items marked).

**Method note:** The failure modes below are pitfalls of *adding the remediations* — not restatements of the findings. Every repo-specific claim was verified in source today: `TransactionManager.cpp` FilterProof (3834–3874), `PublicChainInputValidator.cpp` (328–331 chain-id bypass; 359/460–465 URL-hash corroboration), `GeniusInputValidator.cpp` (443–447 escrow exception), `EscrowTransaction.cpp:106` / `TransferTransaction.cpp:101` (`.value()`), `GeniusNode.cpp:1887` (`sign_messages = true`), `ValidatorRegistry.cpp:785` + `Consensus.cpp` (1474/2504/2589–2600/2712/3244/3347/3390 — `LoadRegistryByCid` call sites and certificate re-validation), fork files (`gossip_core.cpp` 309–361, `message_parser.cpp` 109–121, `relay.cpp` 51–153, ipfs-pubsub `gossip_pubsub.cpp` 317–319), `.github/workflows/cmake.yml` (release at 539–560, tests at 597/606, tag delete/force-recreate at 545–551, artifact download by branch/tag at 318–345), GeniusSDK (`MakeGeniusAddress` at src:204; `test/` holds only CMakeLists.txt). External patterns cite the libp2p pubsub/relay specs, go-libp2p relay resource defaults, a go-libp2p relay DoS advisory, Trail of Bits' finality guide, GitHub required-checks behavior, Apple platform-security docs, and fork-maintenance practice.

**Phase names used below** (proposed to the roadmap; v1.3 phase numbers continue at 21 — keep the order):
- **PA Verification Spine & Release Gate** — adversarial-regression harness; CI release-before-tests reorder; artifact content-hash pinning (#20, #19)
- **PB Consensus & Mint Authorization** — registry-CID trust, escrow entitlement, fail-open chain IDs, legacy-v1 mint, receipt binding, admission/equivocation audit (#1–#5, #14)
- **PC Ingestion & Wire Hardening** — hash-parse errors, fuzzing, fork length-prefix caps (#8, #9)
- **PD Gossip & DHT Authentication + fork-divergence discipline** (#7, #15)
- **PE Bridge Ingestion Safety** — confirmation depth/reorg, content corroboration (#10, #11)
- **PF Processing-Grid Integrity** — proof enforcement, result verification, queue/payout authority, escrow sufficiency (#6, #13, #21, #22)
- **PG Network Topology** — relay/hole-punch or explicit topology (#17)
- **PH SDK, Execution Isolation & Mobile** (#18, #23, #24, #25)
- **PI On-Chain Audit & Return-Leg Activation** (#12, #16) — last, only after the safety boundaries above are proven

---

## Critical Pitfalls

### Pitfall 1: Fail-closed authorization that depends on not-yet-arrived CRDT state

**What goes wrong:**
The fixes for #1 (registry authorization), #2 (escrow entitlement = "certified task result"), and #22 (escrow sufficiency) all make a receive/validation path reject unless some *other* piece of CRDT-propagated state (a registry update, a task certificate, a job dimension record) is present locally. But transactions, certificates, and CRDT data arrive in different orders across peers and across restarts — the exact condition this project's Core Value promises to survive (proven by the Phase 12 four-peer fault suite). A naive fail-closed check rejects legitimate transactions whose entitlement evidence simply hasn't propagated yet: worker payouts stall, certificate tallying fails on some nodes, and the multi-node suite goes intermittently red on propagation order. The symmetrical naive fix — defer/pend everything pending state — creates unbounded in-memory pending queues, which is the memory-exhaustion DoS you just fixed elsewhere (#8).

**Why it happens:**
The vulnerability framing ("don't trust without evidence") translates naturally to "reject on missing evidence," which silently converts an *ordering* problem into an *authorization* failure. Validation code runs per-element on the CRDT ingest path where no ordering guarantee exists between namespaces (registry data vs. tx vs. certificate namespaces propagate independently).

**How to avoid:**
Separate the three verdicts explicitly at each new authorization boundary: **Reject** (evidence present and contradicts), **Defer** (evidence absent, bounded retry with TTL and a hard cap on deferred entries — after which Reject), **Accept** (evidence present and authorizes). Reuse the durable-state patterns this codebase already proved: canonical-slot keys for idempotency, and the wait-predicate test templates from the finality suite for "evidence arrives later" fault tests. Do NOT reach for `DeliverySource::Local` or any local-call provenance as the entitlement — v3.0 already proved that path strands receivers (PROJECT.md records it as a rejected design).

**Warning signs:**
Multi-node tests that pass single-node but flake in `processing_multi`/`processing_nodes`/`bridge_e2e` runs; failures that move between test cases run-to-run; a fix PR whose new adversarial test is green but whose long-standing legitimate-path tests fail only when run last.

**Phase to address:**
PB (verdict model defined with the first authorization fix, reused by all later ones); PA builds the harness that makes the ordering fault tests cheap to write.

---

### Pitfall 2: Closing the registry-CID bypass breaks re-validation of already-stored certificates

**What goes wrong:**
`ValidateCertificate` is not a one-shot check — it is re-invoked on restart, sync, and CRDT merge paths (`Consensus.cpp:2712, 3244, 3347–3353, 3390`; the comment at 1361 confirms every vote signature is re-verified). `LoadRegistryByCid` has eight call sites (four inside `ValidatorRegistry.cpp:1111/1183/1255/1581`, three in `Consensus.cpp:1474/2504/2846`, plus the cached wrapper at 2589–2600). Making the loader fail-closed therefore changes the verdict for **historical finality state**: certificates that validated under the old rule (including any whose registry CIDs were stored via the rejected-data path the assessment flagged) can stop re-validating after the upgrade. Symptom on a live deployment: nodes restart, cannot re-confirm their own certificate store, mint-recovery logic concludes finality is missing, and the exactly-once machinery treats old burns as open. A fresh node syncing history through an upgraded node can likewise fail on old blocks.

**Why it happens:**
The fix is correctly placed in the one owning module (the loader — per the repo's one-owner invariant), which is exactly why its blast radius is every consumer, including the re-validation consumers nobody thinks of as "the vulnerable path." Consensus rule changes are being made in a system with persisted consensus state and no height/context gating on re-validation.

**How to avoid:**
Fix in `ValidatorRegistry`'s loader (all call sites inherit it — do not patch `ConsensusManager` call sites and leave the wrapper or an internal caller un-patched; a partial fix here is a still-forgivable registry). Distinguish three loader outcomes: *authorized* (quorum/genesis-chain verified), *unauthorized* (verifyable as forged — reject and adversarial-test this), *unverifiable at this height/age* (historical registry referenced by an already-finalized certificate — grandfather under an explicit, documented rule tied to certificate finality, not a silent default). Add a compatibility gate to the regression suite: **pre-fix certificates on a pre-upgrade DB must re-validate after upgrade and restart** — this is the paired legitimate-path test for #1.

**Warning signs:**
Existing certificate-store tests failing only after the fix; restart-recovery tests red; fresh-node sync stalling on historical segments; any new code path that returns "registry unauthorized" for a CID that the node itself finalized before the upgrade.

**Phase to address:**
PB (the fix and the grandfather rule); PA (the compat gate in the suite).

---

### Pitfall 3: Disabling legacy-v1 mint orphans historical state and breaks sync/boot

**What goes wrong:**
Legacy `MintTransaction` outputs are live money: UTXOs created by historical v1 mints are still spendable, boot mounting re-derives UTXOs from transaction history (`TransactionManager` boot paths), and fresh nodes sync the full chain. If "disable legacy mint reception" is implemented as reject-at-deserialization or reject-in-`CheckTransactionTypeRules()` unconditionally (`TransactionConsensusHandler.cpp:545–546` still carries `//TODO - Deal with checking the Mint`), then every historical v1 mint in replayed/synced history fails validation: long-lived nodes fail to reach READY after upgrade, fresh nodes cannot sync, and historical outputs become unspendable. The parallel Bitcoin failures are canonical: post-BIP65/BIP16, nodes must apply rules *as they were at that height* when validating history (Zebra RFC-0006 contextual validation exists for exactly this), and CVE-2018-17144 showed the cost of getting validation context wrong.

**Why it happens:**
"Disable reception" reads like a one-line consensus rule, but the rule needs an applicability domain (new transactions vs. historical replay) that the current validation entry points don't distinguish — there is no activation height anywhere in the type rules today (`CheckTransactionTypeRules` returns true for anything without UTXO parameters).

**How to avoid:**
Gate the disable on *admission of new transactions* (mempool/ingress/consensus admission for freshly created v1 mints), never on deserialization of stored/synced history; historical v1 mints must remain valid where they were valid. Write the two paired tests up front: (a) adversarial — a newly self-signed v1 mint is rejected post-cutover with the rejection naming the missing authority; (b) compat — a pre-cutover v1 mint's outputs spend, a node with pre-cutover v1 history restarts to READY, and a fresh node syncs through it. If genesis/test networks rely on v1 mint for seeding, provide the authorized replacement (mint authority path) in the same change or the test networks themselves break.

**Warning signs:**
Boot failures on long-lived nodes only; fresh-node sync stalls at the cutover height; test genesis flows that mint via v1 suddenly failing; someone proposing "just resync the testnets" as the fix.

**Phase to address:**
PB (with the cutover rule in the same phase as the disable — splitting them across phases guarantees an intermediate state that orphans state).

---

### Pitfall 4: Length-prefix caps that fragment the wire protocol across peer versions

**What goes wrong:**
Caps on untrusted length prefixes (#8: uvarint reader, big-endian reader, Kademlia session — three distinct boundaries) change peer behavior asymmetrically once deployed: upgraded nodes reject oversized messages that old nodes still accept, and upgraded *senders* can emit messages old nodes were never prepared to reject. In a mixed-version fleet this partitions gossip/DHT/graphsync by message size — large DAG deltas sync between old peers but silently fail (or get peers blacklisted/backoff-listed) against new ones, producing "works on the dev mesh, breaks on the deployed mesh" failures. A second failure mode is cap value choice: one global constant that is safe for Kademlia RPCs is wrong for graphsync block transfers, and a cap that ignores the *remaining buffer size* still allows the allocation before the bound check.

**Why it happens:**
The finding says "cap lengths before allocation" and the honest minimal fix is a per-reader bound — but the deployment is multi-node with rolling upgrades, and the fork's readers are shared by every protocol. Nothing in the fix itself tells you interop regressed; only mixed-version tests do.

**How to avoid:**
Land the caps in the fork with (a) per-protocol limits derived from what each protocol legitimately carries (wire-struct-derived constants, per the repo's M012/M014 rules — no bare literals), (b) `min(cap, remaining_buffer)` checks before any resize, (c) errors that distinguish "oversized" from "malformed" so senders get a diagnosable failure. In SuperGenius, run a **mixed-version interop matrix** in the multi-node suite (one upgraded peer + one current peer): assert oversized-message rejection is clean (immediate error, no hang, no blacklist escalation), and that legitimate largest-in-practice payloads (big DAG deltas, KUs) still transfer. Decide explicitly whether the cap is also enforced on send (fail fast locally) — it should be.

**Warning signs:**
Interop tests only ever run same-version meshes; graphsync of large blocks failing after the change; peer-blacklist growth in tests; cap constants expressed as magic numbers instead of named per-protocol constants.

**Phase to address:**
PC (caps + mixed-version matrix); the matrix becomes a standing fixture reused by PD/PG (gossip verify and relay also create version skew).

---

### Pitfall 5: Receive-path signature verification doubles CPU on the gossip hot path — and the current validator API can't even express it

**What goes wrong:**
`GossipCore::onTopicMessage` (fork `gossip_core.cpp:300–362`) runs dedup (`msg_cache_.contains`) → validator hook → cache insert → **forward to local subscribers and the mesh, all on the same call**. The validator hook is empty-by-default-assume-valid (325–334), and its callback signature is `validator(msg->from, msg->data)` — the signature and key bytes are parsed by `message_parser.cpp:115–119` but **never passed to the validator and never verified anywhere**. So (a) an app-level validator registered today cannot close #7 at all — the fork's API must change first; (b) once verification is wired, it sits inline before mesh forwarding (which is correct — verifying after forwarding leaves the vulnerability intact), and every unique message pays an asymmetric-crypto verify on the receive thread. At gossip burst rates this is the "doubled CPU" regression: mesh fan-out and heartbeat timers starve, mesh scoring decays, and high-throughput scenarios (`processing_multi`) start timing out.

**Why it happens:**
The libp2p pubsub spec makes strict signing two-axis (create + verify) and go-libp2p defaults `WithStrictSignatureVerification` on — so the fix looks like flipping a default that upstream already solved. But this cpp-libp2p fork has no verification wiring, no peer-scoring infrastructure (gossipsub v1.1 hardening), and no async validation propagation, so the "default" has to be built.

**How to avoid:**
Wire verification in the fork where the bytes already exist (parser or an extended validator signature taking the full signed envelope). Keep the existing msg-id dedup **before** verification (replayed duplicates must not re-verify — the ordering in `onTopicMessage` is already right for this). Then manage cost: per-topic policy (strict verify for public/consensus/processing topics; the private-network membership envelope already enforces those networks), per-peer rate limiting on failing verifies, and if budgets still blow, gossipsub v1.1-style delayed validation (advertise IHAVE, forward only after async verify) rather than verify-after-forward. Add a gossip throughput benchmark to the suite *before* the change so the regression is measurable, not anecdotal.

**Warning signs:**
Benchmark numbers for messages/sec dropping after the change; heartbeat overrun logs; mesh prune/graft churn; tests that only exercise low gossip volume and therefore "prove" nothing about throughput.

**Phase to address:**
PD (API change, wiring, policy, benchmark); PC's mixed-version matrix covers the skew window (signed-required peers vs. unsigned senders during rollout — decide and test whether the window is fail-open by version negotiation or fail-closed by config flag).

---

### Pitfall 6: Authentication without authorization — "verify all gossip signatures" that still trusts the wrong thing

**What goes wrong:**
The team implements "signatures verified on receive" and stops. On a public network, a *valid signature from a key nobody vouches for* is integrity, not authorization — a Sybil attacker signs everything correctly. Conversely, requiring senders to be pre-registered for every public topic either rejects all legitimate traffic (no registration exists yet for public topics) or degrades to signature-only. #13 documents the same self-assertion problem on processing channels (queue updates, payout addresses). The pitfall is treating #7 as a cryptography task when half of it is a *policy* task: which identity classes may publish to which topics.

**Why it happens:**
The finding is phrased "no signature authentication," so the remediation is phrased the same way; the authorization layer (who may publish mint/processing/consensus messages publicly) doesn't exist yet for public networks — the private-network membership filter is the only current authorization mechanism, and the assessment notes it's enforced only there.

**How to avoid:**
Split the fix explicitly: authentication (verify signature before forward, all peers) + authorization (per topic class: consensus/certificate/mint topics require validator-or-registered identity; processing topics require authenticated worker/developer identities per #13; pure-data topics may be authentication-only with peer scoring). Write the policy as data (topic → required identity class), not as scattered branches — the repo's own data-driven rule forbids hardcoded policy chains. Adversarial tests must name the attacker authority (per the suite's discipline): "unsigned peer" is one test, "validly-signed unregistered key" is a different, equally required test.

**Warning signs:**
The fix PR contains only crypto changes and no topic policy table; adversarial tests only cover the unsigned case; public topics that suddenly require a registry that has no public enrollment story.

**Phase to address:**
PD (with the policy table); #13's authenticated processing claims land in PF against the same table.

---

### Pitfall 7: Confirmation-depth gating that stalls the bridge under reorgs

**What goes wrong:**
The finding #10 fix adds "wait N confirmations / finality before irreversible native issuance." Under an actual reorg (or an endpoint that lags), the watcher's receipts flap: the burn receipt appears, the depth gate waits, the chain reorgs, the receipt vanishes, then reappears deeper. Naive implementations (a) hold unbounded receipt state in memory across the wait, (b) treat "receipt gone" as a hard error requiring manual intervention, or (c) treat "not deep enough yet" as retry-forever with no deadline — so a long reorg or a partition stalls the bridge with no operator signal, escrow sits, and users see nothing. Also, a single hardcoded N is wrong per chain: Ethereum PoS has deterministic finality checkpoints (~2 epochs) where N-blocks-of-depth is both too slow and weaker; a probabilistic sidechain needs depth scaled to its reorg risk (Trail of Bits' finality guide; the quant memo model exchanges use).

**Why it happens:**
The finding is "no confirmation-depth requirement," so the remediation is a threshold — the operational states around the threshold (flapping, lag, finality-vs-depth semantics per chain) are invisible in the code path until a reorg actually happens, and tests never generate real reorgs.

**How to avoid:**
Model the gate as an explicit state machine: receipt-seen → depth-tracking (bounded, restart-durable — the burn watcher's evidence must survive restart, consistent with the durable-finality discipline of v3.0) → finalized (proceed to mint) | reorged-out (drop evidence, log, alert) | timeout (escalate, do not silently retry forever). Confirmation policy is per-chain config data (chain-id → finality type + depth), never a C++ constant — the AgentDocs data-driven rule exists for exactly this. Test with a simulated-reorg harness: fork the source-chain simulator mid-wait (an anvil/hardhat-style chain for EVM sides), assert the mint does NOT happen pre-reorg-resolution, the bridge does not wedge post-reorg, and restart mid-wait re-enters the same state. Existing `bridge_e2e`/`bridge_race` suites are the place these grow.

**Warning signs:**
A `kRequiredConfirmations` constant; no state for "reorged out"; tests that only ever advance blocks forward; watcher memory growing during the wait; bridge metrics that show queue depth but no stuck-receipt alert.

**Phase to address:**
PE (state machine + per-chain config + reorg harness).

---

### Pitfall 8: Multi-endpoint corroboration that centralizes into a de-facto single provider

**What goes wrong:**
The current flaw (#11) is that "corroboration" counts endpoint successes and records hashes of **URLs** (`PublicChainInputValidator.cpp:359, 460–465`). The remediation replaces URL-hashes with agreeing **content** hashes — but if the three endpoints are three URLs of one operator (or one provider's regional gateways), content agreement across them is theater: the operator (or its cloud, or a compromise of its stack) still single-handedly authorizes mints. Infura's history makes the point concrete: it is ConsenSys-owned default infrastructure for much of the ecosystem, has had halting outages, and runs on centralized cloud backends — provider diversity, not endpoint count, is the trust property. A second trap: honest-but-diverse endpoints *legitimately disagree* (lagging node, pruned archive, reorg view), and a naive agreement rule then never reaches quorum — the bridge stalls or, worse, someone "fixes" it by relaxing back to success-counts.

**Why it happens:**
Endpoint lists are configured as URLs, and the natural mental model of "multiple RPCs" is redundancy, not independence. Nothing in the config surface records *who operates* an endpoint, so the centralization is invisible to the code doing the corroborating.

**How to avoid:**
Extend the endpoint config schema with operator/provider identity and require quorum across **distinct operators** (fail configuration validation when the corroboration set collapses to one operator — fail fast at startup, per repo rules). Corroborate on canonical content (block hash + decoded event fields, which #5's binding fix already defines — share that canonical decode, don't build a second one: one fact, one field). Define the disagreement policy explicitly: quorum-of-content within a freshness window; stale/lagging endpoints are excluded-with-reason, not counted as no-votes forever; below quorum → defer (Pitfall 1's verdict model), never mint. Adversarial test: N−1 endpoints (all one operator) return fabricated agreeing content and one independent endpoint returns the truth — the mint must not proceed on operator-quorum alone.

**Warning signs:**
Config PRs adding endpoints from the same vendor; corroboration tests where all endpoints are fixtures returning identical canned responses (they can never exercise disagreement); no metric/log for "endpoints disagreed."

**Phase to address:**
PE (schema + quorum semantics), sharing the canonical event decode with the #5 fix in PB.

---

### Pitfall 9: CI release gating that blocks all releases during a red main — and quietly erodes

**What goes wrong:**
Today releases publish before tests run (verified: `cmake.yml` "Create release tag" at 539 and "Upload file" at 560 precede "Run tests" at 597/606). The obvious fix — gate the release job on the test job — creates the lockout the milestone context names: any red (or stuck) develop/main run blocks every release, including hotfixes for the very vulnerabilities this milestone fixes. This repo's CI is already red intermittently (the assessment's October 16 failing run; macOS crash-diagnostics steps exist because flakes happen), and GitHub required checks that never report block indefinitely — GitHub does not time out pending checks. Worse, the current workflow **deletes and force-recreates release tags** (545–551), so "the tests passed for tag X" is not even a stable claim about artifacts. Within weeks the team learns to bypass the gate manually (re-run flaky jobs until green, push the release by hand), and the gate exists only on paper.

**Why it happens:**
Gating on the same mutable ref (develop/main) couples release availability to trunk health, and trunk health includes flaky platforms. The workflow's tag-mutation behavior predates the gate and interacts badly with it.

**How to avoid:**
Reorder so the release job `needs` the full test matrix in the same run (mechanical, do it first), then add the escape valve deliberately: releases cut from an explicit ref (tag or release branch) whose required checks must be green on that ref — a hotfix flows through a short-lived release branch with the same required checks, not through an exception. Fix the tag-mutation smell in the same phase (immutable tags once published; the delete-and-recreate path is both a supply-chain hazard for #19 and a gate-bypass). Pair the gate with a flaky-test budget/triage rule the day it lands, or the flake → blocked-release → manual-bypass loop starts. Verify check *names* match the jobs marked required (mismatched names never report and block forever).

**Warning signs:**
Release cadence dropping to zero after gating; `workflow_dispatch` tag inputs being used as the de-facto bypass; engineers re-running jobs to "get a release out"; release tags whose commit SHA changed after publication.

**Phase to address:**
PA (first phase: the reorder is a prerequisite for trusting everything the later phases ship; the escape valve and tag immutability land with it).

---

### Pitfall 10: Vendored-fork hardening that makes the fork unmergeable with upstream

**What goes wrong:**
The libp2p fork already carries a custom branch (`dev_pnets`) over a detached-HEAD pin, with three remotes (origin `GeniusVentures/libp2p`, `original` `libp2p/cpp-libp2p`, `upstream` `eigendude/libp2p`) and local commits ranging from warning cleanups to feature merges. v1.3 adds more in-tree changes for #8 (reader caps), #15 (validators), #17 (relay/hole-punch completion — building out a 154-line stub into a spec-compliant service). Each change deepens divergence from two different upstreams; within a milestone or two, upstream syncs become merge-conflict festivals, security fixes in upstream stop flowing in, and the effective owner of the P2P stack is this team forever. The classic long-lived-branch failure (community consensus: keep the delta small, patch-like, and rebase frequently).

**Why it happens:**
Every individual fix is legitimately "small," and the fork build (thirdparty superproject with per-platform CI) makes upstream bumps feel optional — nothing forces a sync cadence, so none happens.

**How to avoid:**
Adopt fork-divergence discipline as an explicit deliverable of the fork phase, not a side effect: (a) every fork change is a small, topic-scoped, upstreamable commit (or explicitly tagged `never-upstream: reason`); (b) a `FORK-DELTA.md` (or equivalent manifest) lists every delta with its reason and upstream status — the audit artifact for "how far are we from upstream"; (c) prefer injection over edition: where the app can pass limits/validators/policies in through configuration or interfaces (relay `Resources`, gossip validator registration), add the seam in the fork but keep policy in SuperGenius — smaller delta, and it matches the repo's depend-on-contracts rule; (d) a scheduled upstream-sync task (sync `original`+`upstream`, attempt the rebase, record the conflict cost — even if the merge is deferred, the *measurement* prevents the "we'll sync later" fiction). Relay completion (#17) is the highest-divergence-risk item: implement against the circuit-relay-v2 spec's shapes (`Limit`, reservation semantics) so the code is structurally upstreamable.

**Warning signs:**
`FORK-DELTA.md` stale or absent; fork commits touching files upstream has rewritten; "sync upstream" absent from every recent milestone; deltas that are really features (relay service) rather than fixes.

**Phase to address:**
PD establishes the discipline (it carries the first big fork deltas); PC's caps follow it; PG's relay work is judged against it (if relay completion would blow up the delta, that's the trigger to choose "explicit topology" instead — the finding permits either outcome).

---

### Pitfall 11: Enabling proof verification deletes legitimate transactions (the dead code path is a tombstone path)

**What goes wrong:**
`FilterProof` (verified `TransactionManager.cpp:3834–3874`) currently sets `valid_proof = true; break;` — the bypass. The dead code below it is not a no-op on failure: an invalid proof **tombstones both the `/proof/` element and the mapped `/tx/` element** (3855–3869). Flipping enforcement on means any stored proof that fails `VerifyFullProof` now deletes its transaction from CRDT — if existing proofs don't verify (format drift since they were stored, zkLLVM version skew, placeholder proofs in fixtures and test networks), enforcement doesn't reject new traffic, it *erases historical state*. Separately, `VerifyFullProof` on the CRDT element-ingress path is expensive per element; turning it on for every element can dominate the ingest thread (the same hot-path concern as Pitfall 5, with a much heavier primitive).

**Why it happens:**
The bypass is framed as "verification not enforced," so the remediation is framed as "enforce it." The tombstone side effect is only visible if you read the dead branch carefully — the natural test (submit bad proof, expect rejection) never checks what happened to neighboring state.

**How to avoid:**
Stage enforcement: (1) audit mode — run the verify, log would-fail counts, tombstone **nothing**; (2) quarantine mode — hold failing elements (defer verdict per Pitfall 1) while operators/fixtures are reconciled; (3) enforce — tombstoning only after the audit shows the failing population is genuinely attacker-shaped. Before step 1: inventory existing stored proofs and the zkLLVM proof-format/version situation, and fix the fixtures. Add the paired tests: adversarial (forged/stale proof → rejected, named authority) and compat (all pre-existing valid proofs in the suite's fixtures still verify; their txs remain queryable). Benchmark `VerifyFullProof` on the ingest path and decide (and record) the per-element budget; if it's too heavy, verification moves to a dedicated stage with the ingress path holding-to-quarantine — not back to skip.

**Warning signs:**
The fix PR deletes `valid_proof = true; break;` and nothing else; no audit/compat numbers; proof fixtures in tests are placeholder bytes; after enforcement, `/tx/` lookups failing or CRDT tombstone counts spiking.

**Phase to address:**
PF (the staging plan is the phase's core risk item for #21).

---

### Pitfall 12: Enabling relay/hole-punch on a stub relay — reservations for everyone, limits for no one

**What goes wrong:**
The fork's `relay.cpp` (verified, 154 lines) is not a relay service: the `/libp2p/circuit/relay/0.2.0/hop` protocol handler **logs and drops the stream** (89–95); `onNewConnection` sends a hop reservation toward **every new peer** up to `maxrelays` (114–152); there are no per-peer/per-IP reservation limits, no duration or data caps, no ACL on who may hold a reservation. ipfs-pubsub currently disables relay and both hole-punch roles (`gossip_pubsub.cpp:317–319`, commented wiring at 332–333). "Enable relay" on top of this stub produces either nothing (hop still unimplemented) or, once hop is implemented, a public DoS service — exactly the class go-libp2p hardened after its ≤0.18.0 relay resource-exhaustion advisory, using `Resources` (MaxReservations default 128, per-peer/per-IP/per-ASN caps), the resource manager, and protocol-level `Limit` (duration/data) per the circuit-relay-v2 spec. A second, quieter failure: hole-punch depends on observed addresses (`relay.cpp:67–71` warns and aborts when empty) — a race between reservation and address observation means hole-punch silently never triggers behind exactly the NATs it was enabled for.

**Why it happens:**
The finding says "relay/hole-punch disabled," which reads as a config flip. The code shows the capability is a stub, and topology assumptions (who relays whom, CGNAT/symmetric-NAT behavior) are untested.

**How to avoid:**
Treat #17 as the decision the finding actually allows: **complete relay per the circuit-relay-v2 spec with limits-first** (reservations gated by ACL — e.g., full nodes offer relay service; phones are clients only; per-peer/IP/ASN caps; duration/data `Limit`; connection accounting), or **specify the topology explicitly and document phones-as clients of full-node relays**. If completing: implement limits before openness (a relay with limits and no hop handling is safe; a relay with hop handling and no limits is an incident), and test under symmetric-NAT/CGNAT conditions — not just LAN peers, where hole-punch always succeeds and proves nothing. Add the mixed-version skew test (relay-enabled peer vs. relay-disabled peer, from Pitfall 4's matrix).

**Warning signs:**
A PR that flips `enable_relay = true` with no reservation accounting; hole-punch tests that only run on LAN; relay connection counts with no ceiling; observed-address race reproducing intermittently on emulated NAT.

**Phase to address:**
PG (limits-first implementation order inside the phase; the topology-decision escape hatch is reviewed mid-phase against Pitfall 10's divergence cost).

---

### Pitfall 13: The adversarial suite proves the attacks but not the surviving legitimate paths

**What goes wrong:**
The milestone's verification spine (Key Decision: every test names attacker authority, expected rejection, actual behavior) drives fix authors to write one adversarial test per finding and declare done. Missing everywhere: the **compat companion** — the test proving the legitimate authority still flows through the now-stricter path (Pitfalls 1–3, 11 each depend on one). The suite goes green while the network is broken, and because multi-node propagation-order bugs are timing-shaped (this repo's Phase 12 needed six gap-closure rounds for exactly this class, including the stale-fixture false-green), the false confidence survives local runs and dies only on real deployments. The repo's own history is the evidence: teardown-invariant propagation and stale-fixture DB immunity were hard-won lessons.

**Why it happens:**
Adversarial tests are satisfying to write and unambiguous to review; compat tests need fixtures with realistic pre-fix state, restart choreography, and multi-node ordering — 5–10x the effort of the rejection test.

**How to avoid:**
Make the pair a gate, not a habit: no #finding-fix plan is complete without (a) the adversarial test naming the attacker authority, and (b) the compat test constructing pre-fix state on disk and asserting it survives the fix (re-validate, re-spend, re-sync, restart-to-READY as appropriate). Enforce the fixture rules from Phase 12: fresh temp DBs per test, teardown invariants, and one deliberate negative control proving the assertions can fail. Extend the existing `fuzz/` targets (three exist today: mint deserialize, decode log data, parse burn events) rather than starting a new harness for #9.

**Warning signs:**
Fix PRs whose test diffs contain only `*_rejects_*`/`*_reject*` names; no fixture constructs pre-upgrade persisted state; suites growing sleep-based waits (banned by the repo's wait-condition rule — a sign someone is papering over ordering bugs).

**Phase to address:**
PA (the pair rule and fixture standards are part of the harness itself; every later phase inherits them).

---

### Pitfall 14: SDK memory-safety fixes that break the integrators they protect

**What goes wrong:**
`MakeGeniusAddress` (GeniusSDK.cpp:204, used at 1215–1216) copies an arbitrary-length string into a fixed-size array; `GeniusSDKGetAvailableAccounts` copies fixed byte counts without checking source lengths; `test/` contains only a CMakeLists.txt (all verified). The fix — length-checked copies and consistent ownership conventions — changes the C API's observable behavior (what happens on over-long input? who frees what?). The Unity integration (UnityGeniusSDK, C# wrapper — which the assessment wrongly called nonexistent) and any embedded consumer compile against the current signatures; a silent behavior change (truncate → error, or caller-frees → SDK-frees) becomes a crash or leak **in the integrator's build**, discovered by them. Over-long addresses in the wild may also be *data*, not attacks — hard-rejecting them can break existing deployments that fed the old truncating behavior.

**Why it happens:**
The defects are in a leaf C API with no tests and no documented ownership model, so there is nothing inside the SDK repo that visibly depends on current behavior — every dependency is external and invisible.

**How to avoid:**
Test suite first (the finding demands it anyway): characterize current behavior, then decide and document the new contract per function (status code + out-param for the length-error case; explicit ownership table). Make the safety change additive where possible (new length-aware entry points, old names kept as thin, documented wrappers) and version the C API. Coordinate the Unity wrapper update in the same milestone (it is in scope-adjacent repos under the same planning root). Add ABI/API compat notes to the release checklist from PA.

**Warning signs:**
Fix PRs changing signatures in place with no version bump; no changelog entry; the Unity plugin not built in CI; behavior on over-length input undefined in the header docs.

**Phase to address:**
PH (SDK phase; tests-before-fixes order inside the phase).

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Patch authorization at the `ConsensusManager` call sites instead of the `ValidatorRegistry` loader | Small diff; avoids touching 8 call sites | Partial fix — cached/internal loader callers bypass it; the exact "one owner" invariant violation the repo bans | Never — the loader is the owner |
| Global single length-cap constant across all fork readers | One-line fix per reader | Wrong bound for large graphsync payloads; mixed-version partitions (Pitfall 4) | Never — per-protocol named constants |
| `kRequiredConfirmations` hardcoded for "Ethereum" | Gate ships fast | Wrong (unsafe or stalling) on every other chain; violates the data-driven rule | Never — per-chain config |
| Enforcement flags that default ON in the same commit as the logic | No config surface to maintain | No audit window for tombstone/deletion paths (Pitfall 11); no rollback for consensus-adjacent behavior | Never for state-deleting enforcement; acceptable for pure-reject paths IF the compat test exists |
| Corroboration = N-of-M URL success with content hash bolted on | Minimal change to existing evidence struct | Still operator-countable, not content-quorate; the #11 flaw survives in a new field name | Never |
| Relay enabled by flipping `enable_relay` before limits exist | Topology demo works | Public DoS surface on full nodes (Pitfall 12) | Never on a public network; acceptable only on explicitly isolated test networks |
| Fixtures updated by hand to "make them verify" under new enforcement | Suite goes green quickly | Masks exactly the format-drift the audit mode was supposed to measure (Pitfall 11) | Never — fixture changes are findings, not chores |
| Fork deltas as one squashed "our changes" branch | Fast iteration | Unmergeable fork; upstream security fixes stop flowing (Pitfall 10) | Never — patch-series + delta manifest |

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| Fork `GossipCore` validator hook | Registering an app-level validator assuming it closes #7 — the callback gets `(from, data)`, not the signature | Extend the fork API to pass the signed envelope; verify where parser already holds the bytes |
| Circuit relay v2 (fork stub) | Assuming "spec protocol IDs present" (`/libp2p/circuit/relay/0.2.0/hop` is registered) means relay works — the handler logs and drops | Implement hop handling with `Resources`-style caps + ACL before enabling; or specify topology without relay |
| `gh release download` of thirdparty/zkLLVM artifacts (cmake.yml:318–345) | Verifying a hash of a mutable tag's artifact | Pin by commit SHA + recorded digest; make tags immutable (Pitfall 9's tag-deletion smell) |
| EVM RPC endpoints | Treating endpoint count as corroboration; all endpoints from one operator | Distinct-operator quorum on canonical content (block hash + decoded fields) |
| Hole-punch (fork) | Testing only on LAN; ignoring the observed-address precondition (`relay.cpp:67–71`) | CGNAT/symmetric-NAT emulation; assert hole-punch triggers or fails explicitly |
| GeniusSDK consumers (UnityGeniusSDK) | Changing C signatures/ownership in place | Additive versioned API; coordinated wrapper update; ownership table |
| `ValidateCertificate` re-validation consumers (restart/sync/merge) | Testing only fresh-certificate validation | Compat gate: pre-upgrade certificate store re-validates post-upgrade (Pitfall 2) |
| `fuzz/` targets | Writing a new ingestion fuzz harness from scratch | Extend the three existing targets (mint deserialize, decode log data, burn-event parse) with the malformed-hash corpus for #9 |

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Inline signature verify per gossip message before forward | Receive-thread saturation; heartbeat overruns; mesh decay | Dedup-first (already ordered); per-topic policy; rate limits; async validate-then-forward if budget blown; benchmark before/after | High-rate processing-grid gossip (multi-node job bursts) |
| `VerifyFullProof` per CRDT element on ingress | Ingest thread dominated by ZK verify; sync lag | Audit-mode measurements first; dedicated verification stage with quarantine if over budget | Any node syncing history; restart recovery |
| 3x RPC round-trips per corroboration on the mint path | Bridge latency multiplies; timeouts under endpoint lag | Parallel endpoint queries; freshness window; cached finalized heads | Mainnet load; degraded RPC providers |
| Confirmation-depth wait holding receipt state in memory | Watcher memory growth during reorgs/chain stalls | Durable, bounded depth-tracking state machine | First real reorg or long finality delay |
| Release-gated CI serializing build→test→release | Wall-clock per release grows; queues on self-hosted runners | Keep matrix parallel; gate only the release *publish* step; measure pipeline time in PA | Every release after the gate |
| Length caps checked after allocation attempt | Allocation spike precedes rejection (DoS remains) | `min(cap, remaining)` before resize; fuzz the readers with oversized prefixes | First adversarial peer |

## Security Mistakes

| Mistake | Risk | Prevention |
|---------|------|------------|
| Verifying gossip signatures but authorizing nothing (or authorizing via a registry with no public enrollment story) | Sybil-signed traffic passes; #7 "closed" on paper only | Authentication + per-topic authorization policy as data (Pitfall 6) |
| Grandfathering *everything* unverifiable in the registry fix (silent default instead of explicit rule) | The bypass re-opens through the "unverifiable" verdict | Explicit, documented grandfather rule tied to pre-upgrade finality; adversarial test for post-upgrade unverifiable = reject |
| Fail-open error mapping retained anywhere near the new gates (the #3 pattern: empty/local → success) | Every new validator inherits the same flaw shape | Type-authorized allowlist for local-chain bypasses; adversarial test per new gate with empty inputs |
| Tombstoning `/tx/` state on proof failure before audit | Legitimate transactions erased from CRDT | Staged enforcement (audit → quarantine → enforce) with fixture reconciliation (Pitfall 11) |
| Relay reservations without ACL/limits | Full nodes become public bandwidth relays (resource exhaustion) | Circuit-relay-v2 `Resources` + `Limit` before enabling (Pitfall 12) |
| Mutable release tags + hash-verified artifacts | Verified hash of a replaced artifact | Commit-SHA-pinned digests; immutable tags (Pitfall 9) |
| Partial fork-reader capping (one of three boundaries) | DoS persists via the uncapped reader | Enumerate uvarint, big-endian, Kademlia readers as one unit of work; fuzz each |

## UX Pitfalls (node-operator / integrator facing)

| Pitfall | User Impact | Better Approach |
|---------|-------------|------------------|
| Bridge gated on confirmation depth with no status surface | "My burn vanished" support load during routine finality waits | Explicit mint-state exposure (depth-tracking/finalized/reorged) on the existing API/log surfaces |
| New rejections with generic error strings | Operators cannot tell attack-shaped rejections from misconfiguration | Rejection reasons named after the missing authority (suite discipline made user-visible) |
| Confirmation-depth config in blocks with no per-chain guidance | Operators set unsafe or stalling depths | Per-chain finality-type config schema with documented defaults |
| SDK behavior change (truncate → error) undocumented | Integrator apps crash on legacy data | Changelog + migration note + versioned API |
| Relay topology undocumented after #17 | Mobile integrators assume phone-to-phone mesh that doesn't exist | Published topology statement (who relays, who hole-punches, NAT caveats) |

## "Looks Done But Isn't" Checklist

- [ ] **#1 registry:** forged-registry certificate rejected AND pre-upgrade certificate store re-validates after upgrade+restart AND fresh node syncs history
- [ ] **#2 escrow:** unauthorized spend rejected AND legitimate worker payout passes with certificate arriving *after* the spend (deferred verdict), bounded
- [ ] **#3/#4 mint authority:** empty/local chain-id mint rejected by type AND legacy-v1 historical outputs spendable AND test-genesis flows still seed
- [ ] **#5 receipt binding:** amount/destination/token mismatch rejected AND honest three-endpoint agreement still mints exact amounts
- [ ] **#7 gossip:** unsigned public message dropped before mesh forward AND throughput benchmark within recorded budget AND private-network flows unaffected AND validly-signed-but-unregistered handled per topic policy
- [ ] **#8 caps:** oversized prefixes rejected pre-allocation in **all three** readers (fuzzed) AND mixed-version two-node graphsync of large payloads degrades cleanly
- [ ] **#9 hash parse:** malformed hash returns error (fuzz corpus) AND no `.value()` on network-originated parse results remains (grep-enforced)
- [ ] **#10 depth:** mint does not proceed pre-finality; reorg mid-wait does not wedge the bridge; restart mid-wait resumes state
- [ ] **#11 corroboration:** operator-collapsed endpoint set fails config validation AND fabricated operator-quorum does not mint
- [ ] **#13/#21/#22 processing:** forged result/payout/queue update rejected; enforcement staged with audit numbers recorded; escrow sufficiency holds through price-feed outage
- [ ] **#17 topology:** relay reservations capped and ACL'd (or topology documented as decided); CGNAT case tested
- [ ] **#20 release:** release job needs full test matrix; hotfix path exists that is NOT a manual bypass; tags immutable once published
- [ ] **#19 artifacts:** CI verifies content hash against commit-pinned digest for thirdparty + zkLLVM downloads
- [ ] **#24 SDK:** length-checked copies + real test suite + versioned API + Unity wrapper updated in lockstep

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Fail-closed ordering breakage (1) | MEDIUM | Widen Defer TTL / bound via config hotfix; replay deferred queue; no state loss if quarantine was used |
| Historical certs stop re-validating (2) | HIGH | Ship explicit grandfather rule as point fix; nodes re-verify from durable store (no resync needed if store intact); if store rejected+pruned, resync from full nodes |
| Legacy-mint orphaning (3) | HIGH | Re-introduce height-gated validation (roll forward — consensus rules can't be feature-flagged off after deployment); affected fresh nodes resync |
| Wire fragmentation (4) | MEDIUM | Raise the cap via config/protocol constant roll-forward; re-blacklist cleared peers; mixed-version matrix prevents recurrence |
| Gossip CPU regression (5) | MEDIUM | Flip per-topic policy to relaxed for data topics; keep strict on consensus topics; ship async validation |
| Bridge stall (7) | LOW | State machine timeout → alert path; operator clears stuck receipts; durable state survives restart by design |
| Release lockout (9) | LOW | Release-branch escape valve (the designed path, not a bypass); tag immutability restored |
| Fork unmergeable (10) | HIGH | Reconstruct delta as patch series from FORK-DELTA manifest onto fresh upstream tag; replay conflicts manually |
| Proof tombstoning incident (11) | HIGH | Restore tombstoned `/tx/` elements from full-node CRDT history (replication is the backup); audit-mode forensics; roll enforcement back to quarantine |
| Relay DoS (12) | MEDIUM | Disable relay service flag (clients degrade to direct/full-node routing); add limits; re-enable |
| SDK breakage (14) | LOW | Restore old entry points as wrappers; version-bump; coordinate integrator rebuild |

## Pitfall-to-Phase Mapping

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| 1 CRDT-ordering vs. fail-closed | PB (verdict model), PA (harness) | Out-of-order-arrival fault tests per authorization fix; bounded-defer assertions |
| 2 Cert re-validation breakage | PB (+ PA compat gate) | Pre-upgrade-store re-validation test; fresh-node sync test |
| 3 Legacy-mint orphaning | PB | Height-gated validation tests; historical spend + restart-to-READY |
| 4 Wire fragmentation | PC | Mixed-version interop matrix; per-reader fuzz |
| 5 Gossip verify CPU / API gap | PD | Before/after throughput benchmark; per-topic policy tests |
| 6 Auth-without-authz | PD (policy), PF (processing) | Validly-signed-unregistered-key adversarial tests |
| 7 Bridge reorg stall | PE | Simulated-reorg harness; restart-mid-wait; per-chain config validation |
| 8 Corroboration centralization | PE | Operator-collapse config rejection test; operator-quorum-fabrication adversarial test |
| 9 Release lockout / gate erosion | PA | Release job dependency graph; release-branch hotfix drill; immutability check |
| 10 Fork divergence | PD (discipline), PC/PG comply | FORK-DELTA manifest review; scheduled sync-attempt cost measurement |
| 11 Proof-enforcement state deletion | PF | Audit-mode counts; quarantine behavior; fixture verification inventory |
| 12 Relay stub DoS | PG | Limits/ACL tests; CGNAT emulation; reservation-cap assertions |
| 13 Suite proves attacks only | PA (pair rule), all phases | Compat-companion presence gate per fix plan; fixture teardown invariants |
| 14 SDK integrator breakage | PH | Characterization tests before change; versioned API; Unity wrapper build in CI |

**Ordering rationale:** PA first — the suite's pair rule and the release gate protect every later phase, and the gate must exist before fixes start shipping. PB before PC/PD because authorization fixes define the verdict model and the authorization identities that gossip/processing policies reference. PC before PD because both touch the fork and caps are the simpler delta with which to establish fork discipline (Pitfall 10) before the larger gossip/relay changes. PE can overlap PD but must precede PI (#16 return-leg activation gates on ingestion safety). PF after PB (escrow/result entitlements reuse PB's verdict model and certificate facts). PG late because relay completion is optional-with-escape-hatch and creates its own attack surface only after gossip auth exists. PH's SDK/mobile work is independent but lands after the node-side surfaces it consumes stabilize. PI last — the assessment response's own rule: capabilities activate only after the safety boundaries are proven.

## Sources

**Repo-verified (HIGH confidence, checked 2026-10-19):**
- `src/transaction/TransactionManager.cpp` — FilterProof bypass + `/tx/` tombstone path (3834–3874); boot/nonce scan paths referenced from v1.2 research
- `src/account/PublicChainInputValidator.cpp` — chain-id bypass (328–331), URL-hash corroboration (359, 460–465)
- `src/account/GeniusInputValidator.cpp` — `delegated_escrow_spend` ownership exception (443–447)
- `src/account/EscrowTransaction.cpp:106`, `src/account/TransferTransaction.cpp:101` — unchecked `.value()` on hash parse
- `src/blockchain/ValidatorRegistry.cpp` — `LoadRegistryByCid` (785) + internal callers (1111/1183/1255/1581)
- `src/blockchain/Consensus.cpp` — certificate re-validation call sites (1361 comment, 2712, 3244, 3347–3353, 3390), loader callers (1474/2504/2846), cached wrapper (2589–2600)
- `src/account/GeniusNode.cpp:1887` — `sign_messages = true` (send side)
- Fork `libp2p` (`~/gnus/thirdparty/libp2p`, remotes: GeniusVentures origin, libp2p/cpp-libp2p original, eigendude/libp2p upstream; detached HEAD at 755980f; `dev_pnets` branch) — `gossip_core.cpp` receive path (300–362), `message_parser.cpp` signature/key parsed-not-verified (109–121), `relay.cpp` stub hop handler / auto-reservation / no limits (51–153)
- Fork `ipfs-pubsub` — `gossip_pubsub.cpp` relay/hole-punch disabled (317–319, 332–333)
- `.github/workflows/cmake.yml` — release before tests (539–560 vs 597/606); tag delete/force-recreate (545–551); artifact downloads by branch/tag (318–345)
- GeniusSDK — `MakeGeniusAddress` (src/GeniusSDK.cpp:204, callers 1215–1216); `test/` contains only CMakeLists.txt
- `fuzz/` — three existing targets (mint deserialize, decode log data, parse burn event values); `test/src/` suites (bridge_e2e, bridge_race, processing_multi, processing_nodes, proof, node)
- `.planning/PROJECT.md` — Core Value (out-of-order arrival), rejected `DeliverySource::Local` design, Phase 12 fault-suite discipline; `AgentDocs/AGENT_MISTAKES.md` (M012/M014 wire constants); AgentDocs CLAUDE.md data-driven rule

**External (confidence per item):**
- [libp2p pubsub spec — message signing, two axes](https://github.com/libp2p/specs/blob/master/pubsub/README.md) and [go-libp2p-pubsub WithStrictSignatureVerification](https://pkg.go.dev/github.com/libp2p/go-libp2p-pubsub) — strict receive-path verification is the spec default (HIGH)
- [Gossipsub v1.1 hardening extensions (IPFS blog)](https://blog.ipfs.tech/2020-05-20-gossipsub-v1-1) — peer scoring, extended validators, delayed validation propagation (HIGH)
- [Circuit Relay v2 spec](https://github.com/libp2p/specs/blob/master/relay/circuit-v2.md) and [go-libp2p relay Resources](https://pkg.go.dev/github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/relay) (MaxReservations 128, per-IP/peer/ASN) — relay limits model (HIGH); [go-libp2p ≤0.18.0 relay resource-exhaustion advisory](https://advisories.gitlab.com/pkg/golang/github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/relay) (HIGH)
- [Trail of Bits — The Engineer's Guide to Blockchain Finality](https://blog.trailofbits.com/2023/08/23/the-engineers-guide-to-blockchain-finality); [Arbitrum — Finality and chain reorganizations](https://docs.arbitrum.io/how-arbitrum-works/reference/finality-and-reorgs); [QuickNode reorgs explainer](https://www.quicknode.com/answers/what-is-a-blockchain-reorg); [Quant Memo — finality, reorgs, confirmation depth](https://quantmemo.com/concepts/finality-reorgs-and-confirmation-depth) — per-chain finality semantics vs fixed depths (MEDIUM-HIGH)
- [BIP-16 (pay-to-script-hash activation)](https://github.com/bitcoin/bips/blob/master/bip-0016.mediawiki) and [BIP-65 (CHECKLOCKTIMEVERIFY, height/time-gated activation)](https://github.com/bitcoin/bips/blob/master/bip-0065.mediawiki) — height-gated consensus rules for historical validation (HIGH, official BIP repo); Zebra's contextual (height-dependent) validation RFC-0006 (title-only attribution: exact path not verified, doc surfaced via search at zebra.zfnd.org)
- [GitHub — protected branches / required status checks](https://docs.github.com/repositories/configuring-branches-and-merges-in-your-repository/defining-the-mergeability-of-pull-requests/about-protected-branches) — gate mechanics (HIGH, official docs); community reports of required checks that never report and block merges indefinitely ("Status Checks, Check Runs, and What Blocks a Merge", Runx Build; CircleCI Discuss thread on dynamic configuration vs required checks — title-only attributions, surfaced via search) (MEDIUM)
- [Apple Platform Security — runtime process security](https://support.apple.com/guide/security/security-of-runtime-process-sec15bfe098e/web) and [Stack Overflow — JIT prohibition on iOS](https://stackoverflow.com/questions/5054732/is-it-prohibited-using-of-jitjust-in-time-compiled-code-in-ios-app-for-appstor) — mobile sandbox structural limits (HIGH for Apple doc, MEDIUM for SO)
- [Martin Fowler — branching patterns](https://martinfowler.com/articles/branching-patterns.html); [r/git — keeping a fork updated](https://www.reddit.com/r/git/comments/z01ejf/keeping_a_fork_updated_best_practices); [MinimumCD — long-lived branches anti-pattern](https://beyond.minimumcd.org/docs/anti-patterns/branching-integration/long-lived-feature-branches) — fork/divergence practice (MEDIUM, community)
- Infura centralization/outage coverage: [status.infura.io](https://status.infura.io), [The Defiant — Infura DIN after AWS outage](https://thedefiant.io) — provider-diversity rationale (MEDIUM, news/community)

---
*Pitfalls research for: SuperGenius v1.3 Security Remediation & Network Readiness*
*Researched: 2026-10-19*
