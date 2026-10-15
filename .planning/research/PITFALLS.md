# Pitfalls Research — Light-Node Transaction Pruning (v1.2)

**Domain:** History pruning on a DAG-replicated CRDT store with UTXO semantics (C++17 blockchain)
**Researched:** 2026-10-15
**Confidence:** HIGH for repo mechanics (verified against current source); MEDIUM-HIGH for external patterns (official docs + multiple independent sources)

**Method note:** Every repo-specific claim below was verified against current code this date (`src/crdt/impl/crdt_datastore.cpp`, `src/crdt/impl/graphsync_dagsyncer.cpp`, `src/crdt/impl/crdt_set.cpp`, `src/transaction/TransactionManager.cpp`, `src/account/UTXOManager.cpp`, vendored `ipfs-lite-cpp` block/RocksDB layers). External patterns cite blockchain pruning designs (Filecoin SplitStore, Bitcoin Core prune mode, Solana RPC), RocksDB reclamation semantics, and CRDT tombstone-GC literature.

**Phase names used below** (proposed to the roadmap; rename freely but keep order):
- **P1 Bookkeeping** — tx→CID (and CID→tx) map
- **P2 Erasure Mechanics** — CRDT-value + raw-CID erasure, seal-marker invariant, PRUNED error
- **P3 Policy & Gating** — window, spent-ness gate, anchors, node-type gate, scheduler
- **P4 Pruned-Node Survival** — restart, head-sync, re-delivery guards, serving semantics
- **P5 Reclamation Proof & E2E** — compaction, measured shrink, multi-node fault tests

---

## Critical Pitfalls

### Pitfall 1: Fetch-resurrect loop — physical erasure is not idempotent under CRDT re-delivery

**What goes wrong:**
A pruned transaction's delta block is erased (both CRDT value entries and raw CID block). Later the same delta is re-delivered — restart head exchange (`RequestRelevantHeads`), a lagging peer re-broadcasting an old head, `RetryDueFailedRoots`/`RetryStalledDeltas`, or a full node syncing through us — and the node re-fetches the block from peers and re-merges it, recreating every erased entry. Disk grows back; "pruned" data returns from the dead; the sliding window silently fails.

**Why it happens:**
CRDT merge idempotence is premised on retained state: re-merging the same delta over retained state is a no-op (elements are keyed `/elemsNamespace/<key>/<block_id>`, so a re-put hits the same key). Physical erasure breaks that premise — the re-put CREATES keys that were deleted. Compounding this, `CrdtDatastore::HandleRootCIDBlock` → `CreateRootJob` (crdt_datastore.cpp:774–818) calls `dagSyncer_->getNode(cid)` and merges with **no `isResolved` guard at root entry** — today that is harmless (re-merge is idempotent); with erasure it becomes a resurrection path. The same is true of the whole CRDT tombstone-GC literature: reaping is safe only once "every replica has surely seen the delete" — a causal-stability barrier that a light node cannot prove for arbitrary lagging peers (Riak/Antidote/ScyllaDB resurrection-bug class; ScyllaDB "Preventing Data Resurrection with Repair-Based Tombstone GC").

**How to avoid:**
Keep a durable, per-CID "already processed/pruned" marker that survives both erasure and restart, and gate root-job creation on it. The mechanism already exists: `RocksdbDatastore::seal()` writes a **separate key** `<cid-string>sealed` that `remove()` does not touch (datastore_rocksdb.cpp:48–65), and `isResolved` is checked in traversal. The pruner must (a) NEVER erase seal markers, and (b) add an `isResolved`/pruned-set check in root processing so re-delivered roots are skipped instead of fetched+merged. Verify with a dedicated test: after pruning, re-broadcast the pruned root CID (and restart + head-exchange) and assert zero network `getNode` fetches and zero re-created elems.

**Warning signs:**
"getNode begin for node ... from Root Job" log lines for months-old CIDs after a prune; elems count or disk creeping back up hours/days after pruning; a "pruned" transaction queryable again after restart.

**Phase to address:**
P2 (the guard + seal-marker invariant) and P4 (the re-delivery fault tests).

---

### Pitfall 2: Erasing a raw CID block that carries more than the pruned transaction

**What goes wrong:**
One delta block carries a **vector** of elements, all persisted under the same block id: `CrdtSet::PutElems(std::vector<Element>&, aID, priority)` writes `/elems/<key>/<aID>` for every element in the delta (crdt_set.cpp:669–735), and the raw protobuf bytes for ALL of them live under the single delta CID in the ipfs-lite block store. A tx→CID map that erases the raw block when ONE transaction is prunable destroys the carrier for: other accounts' transactions in the same delta, certificates, SecureCRDT policy values, or tombstones for still-live keys. Traversal from retained heads then hits a missing block (see Pitfall 3), and a fresh node syncing from a full node cannot re-derive our retained elements' history.

**Why it happens:**
The tx→CID direction is the one the milestone names (the CID already arrives with the element in `PutElementsCallback(key, value, cid)`), so building only that map feels sufficient. The reverse direction (CID → set of retained elements) is the one that actually gates safe erasure. Note also that light nodes filter at delta-processing time (`FilterElementsOnDelta`, crdt_datastore.cpp:961–983) — they store raw blocks containing elements they never applied, so "elements I applied" is NOT the same set as "elements in the block".

**How to avoid:**
Build the bookkeeping as a **bidirectional map**: tx→CID for erasure, CID→element-set (or a retained-refcount) computed from the elems namespace at prune time. Erase the raw CID block only when the delta's retained-element set is empty (or refcount hits zero). The same gating must cover the CRDT-value side: erasing `/chain/<net>/tx/<hash>` value/elems entries while the delta block stays is also only half a cleanup, but the reverse (block gone, values kept) is the correctness bug.

**Warning signs:**
Peers' graphsync requests failing with `UNRESOLVED_LINK` ("MerkleDAG service: broken link" — merkledag_service_impl.cpp:137–146); fresh-node sync stalling mid-walk; certificates or policy values disappearing on the light node.

**Phase to address:**
P1 (design the bidirectional map before any erasure code exists).

---

### Pitfall 3: Deleting a block that retained deltas link to — traversal fetch storms and the pinned single DAG worker

**What goes wrong:**
The delta DAG is a chain: every retained delta block links to its prior heads. If a mid-chain block is erased AND its seal marker is removed (or the marker path is collateral damage of "clean up everything related"), then `TraverseCIDsLinks` (graphsync_dagsyncer.cpp:683–797) fails `GetNodeWithoutRequest(child)`, classifies the CID as `links_to_fetch`, and the node issues graphsync requests for the old CID. If peers that still have it answer, the block is re-added (resurrection, Pitfall 1); if the reachable peers are pruned light nodes, the request stalls IN_PROGRESS. The archive-replication flake (repo memory, Sept 2026) proved the operational consequence: the node's DAG worker pool can be pinned by a stalled `getNode` poll loop (100 ms polling, failover at ~120 s), with recovery only via the 30 s periodic head re-broadcast — with `numWorkers` small (configurable, default from `CrdtOptions`), one storm makes the node look hung.

**Why it happens:**
Traversal order is: `isResolved(child)` → skip; else `GetNodeWithoutRequest(child)` → local block; else network fetch. Seal markers ARE the "don't need this block" signal — but they live in the same RocksDB as the blocks, in a non-obvious string-key form (`<cid-string>sealed`), so a pruner that iterates/deletes by CID-encoded key ranges can miss them, and a well-meaning "erase everything for this CID" helper will delete both.

**How to avoid:**
Hard invariant: **the pruner erases raw block keys (CID-encoded bytes) only, never the `<cid-string>sealed` markers** — sealed markers are the traversal terminator that makes a pruned chain walk stop. Codify as a checked invariant: for every child link of every retained delta, `isResolved(child) == true` after a prune run. Add a test that walks the DAG from live heads after pruning and asserts no `links_to_fetch` entries older than the window.

**Warning signs:**
Repeated "Missing block {}, adding as link to be fetched" for old CIDs; IN_PROGRESS graphsync requests with no terminal status; recovery only on 30 s head-rebroadcast boundaries (the flake signature); DAG job latency spikes.

**Phase to address:**
P2 (invariant + helper API that makes wrong deletion impossible), P4 (post-prune walk test).

---

### Pitfall 4: UTXO set vs transaction record divergence — spent-ness observed but later reverted

**What goes wrong:**
Spent-ness lives in `/utxo/<addr>/<txid>:<idx>` records (`UTXO_ENTRY_CONSUMED`, `spent_epoch`, `spent_by_txid` — UTXOManager.cpp:18–34, 285–390), which are themselves CRDT data. The pruner samples "all outputs spent" at decision time and erases the creating transaction. A late-arriving or equal-priority competing delta can revert consumed→READY (the repo's acknowledged deferred "CRDT equal-priority overwrite guard" gap makes equal-priority overwrites a live hazard). The node then holds a READY UTXO whose creating transaction is gone; at boot, `FetchAndProcessTransaction(tx_path)` fails for it, the hash lands in `missing_tx_hashes_` (TransactionManager.cpp:3220–3247), and the node cannot cleanly reach READY — the exact "restart recovery expects history the pruner removed" failure.

**Why it happens:**
Spent-ness is mutable CRDT state, not an immutable fact; treating a point-in-time observation as permanent is a TOCTOU error across the sync window. Bitcoin's equivalent constraint: a pruned node cannot validate a reorg deeper than its retained window — the window is exactly the mechanism that bounds "spent-ness might still change" (Blockstream docs).

**How to avoid:**
Gate erasure on BOTH (a) all outputs spent AND (b) the depth/finality window elapsed, where the window is measured in finalized epochs/certificates — never wall-clock — so any reorg/duplicate/conflict resolution inside the window finds its inputs still present. Re-verify the spent-ness of every output **immediately before** each erase (under the same lock the UTXOManager uses), not from a precomputed candidate list. Never erase a transaction referenced by any READY/RESERVED UTXO or a UTXO checkpoint (`/utxo-checkpoint/...`). Fault test: inject a late conflicting spend at window-edge and assert inputs are still present.

**Warning signs:**
UTXO states flipping CONSUMED→READY after sync; "Incomplete history"/missing-tx errors at boot; node failing to reach READY after restart on a long-running light node only.

**Phase to address:**
P3 (gate + window definition), P5 (fault test).

---

### Pitfall 5: Erasing the nonce-chain anchor — new transactions diverge after restart

**What goes wrong:**
Every outgoing transaction references the previous-nonce transaction's hash. When the in-memory `tx_processed_m` map is cold (restart), `GetPersistedOutgoingPreviousHash` **scans all persisted transactions** under `/chain/<net>/tx` to recover the previous hash (TransactionManager.cpp:1602–1649). If the previous-nonce transaction is fully spent and older than the window, the pruner erases it; the fallback scan finds nothing; the new transaction gets an empty/incorrect `previous_hash`. This repo has already seen "nonce-chain divergence: light node minted on a pre-arbitration head" cause a **deterministic four-platform CI failure** (PR-review fix 93eb51e69, reverted in 55276c912 — cert-receipt memory). Pruning reintroduces the same divergence class.

**Why it happens:**
The nonce chain is an implicit dependency between the wallet's entire outgoing history and new-transaction creation; nothing in "all outputs spent + old" protects it. The scan-based fallback only runs on cold start, so tests that don't restart after pruning never see it.

**How to avoid:**
Per-address anchor rule in the pruner: never erase the highest-nonce certified transaction per own address (and ideally the last K by nonce, K ≥ 1), regardless of spent-ness or window age. Add a restart test: prune → restart → create a new transaction → assert `previous_hash` equals the retained anchor's hash and peers accept the nonce chain.

**Warning signs:**
New transactions with empty `previous_hash` after a restart-following-prune; peers/validators rejecting the nonce chain only on long-lived light nodes.

**Phase to address:**
P3 (anchor rule), P4 (restart test).

---

### Pitfall 6: Restart recovery and head-sync expecting removed history — loops and re-fetch storms

**What goes wrong:**
Beyond Pitfall 1's resurrection: restart triggers initial head-sync recovery (~30 s in the archive-repl evidence); peers respond with head sets that may include CIDs the light node pruned or never had. Without the root-entry guard, each such CID becomes fetch work; failed fetches feed the retry queues (`failedRootRetries_`, stalled-delta retries — crdt_datastore.cpp:118–120, 1751–1798), producing a bounded-but-ugly storm; and `CRDTWorkJournal::RecoverStaleProcessing` (crdt_work_journal.cpp:150) must not resurrect journal entries whose targets were erased. Worst case is a ping-pong: pruned node requests from peers that still have data → gets it back → erases again → repeat.

**Why it happens:**
Every existing recovery path was written under the invariant "history never disappears." Recovery correctness after erasure requires new knowledge (what was pruned) to be durable and consulted by every recovery path — heads, work journal, retries, TransactionManager missing-tx handling.

**How to avoid:**
Persist the pruned-CID knowledge (the seal markers plus the tx→CID map's "pruned" records) in the same RocksDB so it survives restart atomically with the erasure. Audit every recovery consumer: root-job creation, `RetryDueFailedRoots`, `RetryStalledDeltas`, work-journal recovery, and `missing_tx_hashes_` handling — each must treat pruned CIDs as terminal, not fetchable. The restart test matrix: prune → restart → assert (a) node reaches READY, (b) zero network fetches for pruned CIDs, (c) new-tip sync works.

**Warning signs:**
Post-boot log floods of getNode/retry lines for old CIDs; disk usage rising right after restart; intermittent boot stalls that recover at multiples of the 30 s head-rebroadcast interval.

**Phase to address:**
P4 (this is the phase's core purpose).

---

### Pitfall 7: RocksDB space does not actually shrink — claiming the win on key counts

**What goes wrong:**
Deletes in RocksDB write tombstones; disk space is reclaimed only when compaction merges tombstones to the bottommost level. With no new writes, RocksDB may never schedule that compaction ("interlock" — RocksDB dev discussions; SO "RocksDB is not freeing up space after a delete"). Two databases are involved — the CRDT datastore RocksDB (values, elems, tombs, heads) and the ipfs-lite block-store RocksDB (raw CID blocks) — and **neither wrapper exposes a compaction API** (verified: `sgns::ipfs_lite::ipfs_lite::rocksdb` and `src/storage/rocksdb` expose put/get/contains/remove only). A test that asserts `Get(cid)` fails after pruning passes while `du(1)` shows 100% of the original size.

**Why it happens:**
The RocksDB API surface makes logical delete and physical reclamation feel like one operation; the wrappers were never asked to compact. Filecoin's SplitStore treats compaction as a first-class subsystem for exactly this reason.

**How to avoid:**
Extend both wrappers with an explicit compaction entry point (`DB::CompactRange` with `bottommost_level_compaction` / `full_compaction` options) and invoke it after prune batches (rate-limited; compaction is IO-heavy). Make the success metric **measured bytes**: SST file sizes / `du` of the DB directory before vs after, asserted in CI — not deleted-key counts. Report the reclaim number (bytes) in the pruner's log line so operators see the truth.

**Warning signs:**
PR claims bounded disk but operator dashboards show flat usage; `total-sst-files-size` unchanged after prune; compaction-stall warnings under prune load.

**Phase to address:**
P5 (wrapper extension + measured-shrink gate). The wrapper API decision belongs in P2's design so erasure code isn't rewritten.

---

### Pitfall 8: Pruning on full/archive nodes, or pruning non-transaction CRDT data

**What goes wrong:**
`is_full_node_` is a derived bool that already gates several unrelated behaviors; a pruning gate keyed on it (or worse, on a config flag defaulting on) can prune an archive node — destroying the network's last copy of history. On the data axis, a prefix-delete one level too broad (`/chain/<net>/` instead of `/chain/<net>/tx/`) erases certificates at `/cert/<canonical-slot>`, SecureCRDT policy values, and registration records (`reg/<addr>`), breaking the burn-finality exactly-once guarantees that v3.0 spent twelve phases proving. Namespaces that must NEVER be pruned: `/utxo/`, `/utxo-checkpoint/`, `/cert/`, heads namespace, seal markers, policy/registry CRDT values, processing-grid results.

**Why it happens:**
Two independent axes (node-role gating and keyspace scoping) each look trivial; the failure is their combination in one batch-delete call. Bitcoin's equivalent foot-gun is real: GUI/config interactions silently re-enabling prune mode forced full resyncs (bitcoin-core/gui #709); prune+txindex is a hard error there for the same "index points into pruned data" reason.

**How to avoid:**
Gate on the `NodeType` enum (== Light) directly, not the derived bool, evaluated at pruner construction; fail-closed (no prune unless explicitly Light). Constrain erasure to an explicit key-prefix allowlist (`/chain/<net>/tx/` and, if scoped, own-address registrations) asserted in code and in a unit test — every other namespace is rejected by construction. Add two gates: (a) a full/archive node with the pruning config present retains 100% of transactions (ctest), (b) a light-node prune run leaves certificates, policy values, heads, and seal markers byte-identical (ctest).

**Warning signs:**
Full-node test DBs shrinking; certificate NOT_FOUND after a prune run; archive test failures only when the pruner is compiled in.

**Phase to address:**
P3 (gating), P5 (the two ctest gates).

---

### Pitfall 9: Historical queries answered with wrong results instead of an explicit "pruned" error

**What goes wrong:**
After erasure, `db.Get({tx_key})` returns the same NOT_FOUND as "never existed". Consumers that branch on absence make wrong decisions: nonce recovery treats it as "no previous transaction", certificate matching treats it as "no certificate", and any API/RPC history surface reports "transaction not found" — each is a wrong answer for data that verifiably existed. The UTXO/boot path in Pitfall 4 is the most dangerous consumer. Externally, Solana's `getTransaction` returning `null` for both "doesn't exist" and "pruned" is the documented ambiguity operators hate; non-archive nodes there emit explicit ledger-pruning errors instead.

**Why it happens:**
The storage layer's error vocabulary predates erasure; NOT_FOUND was unambiguous when history was immutable and never deleted.

**How to avoid:**
Distinguish PRUNED from NOT_FOUND at the boundary: the tx→CID bookkeeping already knows what was pruned — persist a minimal pruned-key record (or consult the pruned-CID set) and return a distinct error code from the tx-fetch path; map it to an explicit "pruned on this light node; query a full/archive node" at API surfaces. Test: a pruned tx query returns the PRUNED error, not NOT_FOUND, and a never-existed tx still returns NOT_FOUND.

**Warning signs:**
API consumers unable to distinguish pruned from nonexistent; support tickets about "missing" old transactions on light nodes only.

**Phase to address:**
P2 (error surface defined with the erasure mechanics), P4 (consumer audit — nonce recovery, cert matching, boot mounting).

---

### Pitfall 10: Pruner racing in-flight transactions and the certificate pipeline (batch-commit ordering)

**What goes wrong:**
This repo has a proven failure mode where speeding up work after a CRDT callback fired **before the CrdtSet batch committed** caused deterministic divergence (cert-receipt memory, 2026-10-12): callbacks fire from the element path pre-commit; the up-to-500 ms deferred dispatch via timer tick is the ordering guarantee that RocksDB committed first. A pruner triggered from (or racing with) element callbacks can read uncommitted or half-committed state, or erase between the UTXO mutation and its record persistence (the UTXOManager's own acknowledged non-atomic record path). Separately, a transaction with RESERVED (in-flight spend) UTXOs must block erasure of both the spending tx and the tx creating the reserved inputs.

**Why it happens:**
The natural implementation hook for "a transaction became fully spent" is the CRDT callback — exactly the wrong place per the recorded invariant. Debug-vs-release timeout differences (`SGNS_DEBUG` selecting 30 s vs 50 s) can additionally hide the race in testing.

**How to avoid:**
The pruner runs as an independent periodic task reading **committed state only** (post-batch), never on the callback path; erase decisions take the relevant locks (`tx_mutex_m`, UTXO manager lock) and re-verify in the critical section immediately before deletion; RESERVED/CONSUMED-in-flight states block candidate eligibility until finalized. Follow the repo rule: any dispatch speedup after CRDT receipt must first prove batch-commit ordering. Test: spend arriving at the window edge while the pruner cycle runs (and the inverse), with the pruner enabled in both debug and release timing profiles.

**Warning signs:**
Intermittent failures only when the pruner is enabled; races that reproduce on one platform only; failures clustering around the 500 ms deferred-dispatch interval.

**Phase to address:**
P3 (scheduler design + locks), P5 (race fault tests).

---

### Pitfall 11: Test-fixture RocksDB reuse masking erasure (and resurrection) bugs

**What goes wrong:**
Fixtures that reuse a DB directory across runs (or across test cases) can pass while erasure is broken: the test reads stale data left by a previous run and "verifies" it, or a resurrection bug re-adds data that never left, or a fresh-run failure is masked by leftovers. This repo already paid for this class in v1.1 Phase 12 ("stale-fixture db immunity" gap-closure round) and standardized teardown-invariant propagation for exactly this reason.

**Why it happens:**
Long-lived integration fixtures are cheap to reuse; erasure tests are the one category where a dirty fixture produces a false green with high probability, because "data present" is the pre-condition of every earlier test.

**How to avoid:**
Every pruning test uses a fresh temp DB (unique per run); assertions are three-part: present-before, absent-after (both stores: CRDT value AND raw block `contains`), seal-marker-still-present; and teardown asserts the fixture left nothing behind (the Phase 12 teardown-invariant pattern). Add an explicit negative control: one test where erasure is deliberately skipped and the absence-assertion fails, proving the assertions can fail.

**Warning signs:**
Pruning tests green locally but disk-bound behavior wrong in the field; tests passing on a developer machine but failing on freshly-provisioned CI runners; "cannot open DB" flakiness from concurrent fixtures.

**Phase to address:**
P5 (test-harness rules), enforced from P2's first erasure test onward.

---

### Pitfall 12: Broken serving for peers — fresh-node bootstrap stalls against pruned light nodes

**What goes wrong:**
A retained head's block links to pruned deltas. When another peer asks a pruned light node to walk such a subtree, the responder's `MerkleDagServiceImpl::select` returns `UNRESOLVED_LINK` ("broken link") once traversal hits a missing block (merkledag_service_impl.cpp:44–63) — the request fails partway, graphsync-style (analogous to go-graphsync `RequestFailedPartial`: blocks delivered until the traversal error). A fresh light node that happens to route through pruned light nodes experiences stalls, blacklist/backoff churn, and possibly a fetch cascade where each pruned peer fails it in turn. The milestone correctly makes light-node historical serving a non-goal — but nothing today STOPS peers from asking, because light nodes still receive and re-broadcast heads.

**Why it happens:**
Head re-broadcast advertises "I have this head"; peers can't know the advertising node is pruned beneath it. Filecoin's SplitStore solves the serving side with the cold store + snapshot bootstrap rather than trusting pruned nodes to serve.

**How to avoid:**
Treat "fails fast and cleanly" as the requirement: responder errors on pruned CIDs must be immediate `not found`/partial-fail (never hangs), and the requester's failover must move to the next peer — which the existing blacklist/backoff already does; verify it does so promptly for this error class. Ensure fresh-light-node bootstrap prefers full nodes (`bootstrap_fullnodes` already exists in `sgns_config.json`). E2E test: fresh light node joins a mesh where every existing light node has pruned below the fresh node's start height; it must sync from the full node and reach READY without manual intervention.

**Warning signs:**
Fresh-node syncs stalling with "broken link" errors; pruned light nodes accumulating peer blacklist entries; bootstrap succeeding only when a full node is first in the peer list.

**Phase to address:**
P4 (serving semantics + failover verification), P5 (the fresh-node E2E).

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| One-directional tx→CID map only | Smaller bookkeeping; faster first phase | Shared-delta erasure corruption (Pitfall 2); rewrite when first multi-element delta prunes wrong | Never — bidirectional from day one |
| Erase CRDT values, skip raw CID blocks | Simplest visible win (values are large) | Block store keeps growing; milestone goal missed; second erasure pass later re-opens every risk | Never for the milestone claim; acceptable as an intermediate commit only if the phase gates on raw-block erasure later |
| Erase seal markers "for completeness" | Marginally smaller DB (string keys are tiny) | Fetch-resurrect loops and traversal storms (Pitfalls 1, 3) | Never |
| Wall-clock window ("older than N hours") | No epoch plumbing | Window races finality under load; reorg-window violations | Never — finality-epoch based |
| Prune in one giant batch per cycle | Simple scheduler | Huge RocksDB WriteBatch, compaction storm, latency spike for live traffic; iterator invalidation if deleting during `QueryKeyValues` iteration | Never — bounded batches, collect-then-delete |
| Reuse existing integration fixtures for pruning tests | Fast to write | False greens from stale DBs (Pitfall 11) | Never |
| Log line "pruned N transactions" as the success metric | Trivial | Claims the win while disk is flat (Pitfall 7) | Acceptable only alongside the measured-bytes metric |
| Skip full/archive retention gate "because config defaults off" | One less test | One config mistake destroys network history (Pitfall 8) | Never — the ctest gate is cheap |

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| ipfs-lite `RocksdbDatastore` | Assuming `remove(cid)` clears "everything for that CID"; or erasing the `<cid>sealed` marker | `remove` deletes only the CID-keyed block; the seal marker is a separate string key and must outlive the block (traversal terminator) |
| `GraphsyncDAGSyncer` | Reusing `DeleteCIDBlock` for pruning — it only removes the in-memory LRU cache entry | Physical erasure needs a new path through the DAG service / block service to the persistent store; LRU deletion is unrelated |
| `CrdtSet` elems/tombs namespaces | Erasing elems but keeping tombs (or vice versa) for the same key | Erase per-key entries atomically as a set; a tombstone without its element (or reverse) skews add-wins resolution on any re-delivery |
| `MerkleDagServiceImpl::select` (responder) | Assuming partial subtrees serve "mostly fine" | Any missing link fails the request (`UNRESOLVED_LINK`); design for clean fast failure and requester failover |
| RocksDB (both instances) | Expecting deletes to shrink files; assuming one DB covers everything | Explicit `CompactRange` after prune batches; the CRDT store and the block store are separate DBs — reclaim both |
| CRDT callbacks | Triggering prune work from element callbacks | Callbacks fire pre-batch-commit (proven invariant, cert-receipt memory); pruner reads committed state on its own schedule |
| `TransactionManager` boot paths | Assuming boot only needs the UTXO set | Boot re-fetches transactions for READY UTXOs, scans persisted txs for nonce recovery, and mounts UTXOs from tx history when local UTXOs are empty (`QueryTransactions`) — all must tolerate PRUNED |

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Prune cycle deleting while iterating `QueryKeyValues` | Iterator invalidation / missed or double-deleted keys; RocksDB snapshot errors | Collect candidates, verify, then delete in bounded batches | First large window flush |
| Single huge delete batch + immediate full compaction | Multi-second IO stall; pubsub/DAG worker starvation | Rate-limited batches; incremental compaction (per prefix range) | ~100k+ pruned entries per cycle |
| Compaction competing with the single/few DAG workers | Sync latency spikes; test timeouts | Run compaction off the DAG worker threads; make it interruptible | Any prune run on busy node |
| Pruner re-scanning all txs every cycle (O(history) scans) | Constant background CPU on long-lived nodes | Maintain a candidate index (spent-and-finalized watermark) in the bookkeeping map | Weeks of uptime |
| Seal-marker keyspace growth unbounded | Slow creep of tiny keys | Accept (they are the pruning enabler); monitor; document expected size (~CID string length per delta) | Very long uptimes with high tx rate |
| Depth window computed by scanning certificates each cycle | Certificate-store hot-path contention | Cache the finalized-epoch watermark, advance monotonically | Consensus-sensitive tests timing out |

## Security Mistakes

| Mistake | Risk | Prevention |
|---------|------|------------|
| Pruning a transaction a still-contested certificate references (or the certificate itself) | Double-mint / second certificate for one burn — the core v3.0 invariant | Certificates live outside the tx namespace and are never pruned; pruner never crosses the allowlist; ctest gate on cert-store immutability across prune runs |
| Erasing spent-ness evidence (the consumed UTXO record) along with the tx | Re-spend of already-spent outputs becomes undetectable locally | Only the tx record + delta block are pruned; `/utxo/` consumed records and tombstones needed for double-spend rejection are retained (their retention policy is a separate, explicit decision — default keep) |
| Accepting a re-delivered pruned delta after a long partition (resurrection) | An attacker or lagging peer re-injects spent history; disk-fill vector | Pitfall 1's root-entry guard makes re-delivery terminal regardless of peer behavior |
| Pruner configuration injectable over RPC/config without role validation | Remote-triggered history destruction on a misconfigured node | Node-type gate is read-only post-construction (immutable `NodeType`); no runtime toggle |
| Reporting "pruned" for a transaction that was never on-chain (bad key hygiene in the map) | Wrong answers erode trust in light-node queries | PRUNED state derives from an actual erasure event recorded in the map, not from absence |

## UX Pitfalls (node-operator facing)

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| "Pruned N transactions" log with flat disk | Operator loses trust; support load | Log measured bytes reclaimed (post-compaction) and current DB size |
| NOT_FOUND for historical queries on light nodes | Users conclude funds/history lost | Explicit PRUNED error naming the remedy ("query a full/archive node") |
| Window config in seconds with no finality explanation | Operators set tiny windows; nodes flap on late conflicts | Config documented in finalized epochs with minimum bound enforced |
| Pruning enabled silently by default on upgrade | Archive operator upgrades and loses history | Default off for all roles; explicit `node_type: Light` + pruning consent required |

## "Looks Done But Isn't" Checklist

- [ ] **Erasure:** value gone AND raw block gone — verify `contains(cid)` false in the block store, not just `db.Get(tx_key)` erroring
- [ ] **Seal markers:** still present after prune — verify `<cid>sealed` key exists for every pruned CID
- [ ] **Disk:** DB directory bytes (SST totals) measurably smaller after compaction — not key-count assertions
- [ ] **Both stores:** CRDT datastore RocksDB AND ipfs-lite block-store RocksDB both reclaimed
- [ ] **Restart:** pruned node restarts, reaches READY, syncs new tips, spends/receives — with zero fetches of pruned CIDs
- [ ] **Re-delivery:** re-broadcast of a pruned root CID is a no-op (no fetch, no re-created elements)
- [ ] **Nonce chain:** new transaction after restart references the correct previous hash
- [ ] **UTXO set:** unspent set byte-identical across a prune run; READY UTXO boot mounting intact
- [ ] **Full/archive:** retain 100% of transactions with pruning config present (ctest gate)
- [ ] **Non-tx data:** certificates, policy values, heads, checkpoints byte-identical across a prune run (ctest gate)
- [ ] **Fresh node:** a new light node bootstraps from a full node while existing light nodes are pruned
- [ ] **Errors:** pruned-tx query returns PRUNED, never-existed returns NOT_FOUND
- [ ] **Fixtures:** pruning tests use fresh temp DBs; teardown invariants verified; negative control proves assertions can fail

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Resurrection loop (1, 3) | MEDIUM | Stop node; run a scrub pass re-applying the pruned set (delete re-created entries + restore seal markers); if seal markers lost wholesale, re-sync from a full node into a fresh DB |
| Wrong block erased (2) | HIGH | Light node: wipe DB and re-sync from full node (history is recoverable from full/archive by design). Full/archive hit: restore from backup — this is why the full/archive gate is a ctest, not a hope |
| Disk never shrank (7) | LOW | Add compaction pass; run `CompactRange` over pruned ranges; no data risk |
| Nonce divergence after prune (5) | MEDIUM | Restore anchor tx from full node (targeted re-fetch of own-address history), or re-derive anchor from certificate store; then prune rule fixed going forward |
| Node can't READY after restart (4, 6) | MEDIUM | Boot with pruning disabled; targeted re-fetch of missing-tx hashes from full node; repair UTXO state from checkpoints; then fix the gate that let the dependency prune |
| Full/archive pruned (8) | VERY HIGH | No local recovery — network-level incident: restore from another archive's snapshot; announce; coordinated resync of affected light nodes |

## Pitfall-to-Phase Mapping

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| 1 Fetch-resurrect loop | P2 (guard + seal invariant), P4 | Re-broadcast + restart test asserts no fetch/no re-created elems |
| 2 Shared-delta block erasure | P1 (bidirectional map) | CID→element-set gate unit test; multi-element delta fixture |
| 3 Dangling-link traversal storm | P2, P4 | Post-prune DAG walk asserts no links_to_fetch under window |
| 4 Spent-ness divergence | P3, P5 | Late-conflict-at-window-edge fault test; boot-READY assertion |
| 5 Nonce-anchor erasure | P3, P4 | Restart + new-tx previous_hash assertion |
| 6 Restart/head-sync storms | P4 | Restart matrix: READY, zero pruned-CID fetches, tip sync |
| 7 Disk not reclaimed | P5 (API decided in P2) | Measured-bytes CI gate on both DBs |
| 8 Wrong node/data pruned | P3, P5 | Full-node retention ctest + non-tx-namespace immutability ctest |
| 9 Wrong historical answers | P2, P4 | PRUNED-vs-NOT_FOUND error test; consumer audit |
| 10 Pruner/commit-order race | P3, P5 | Window-edge race tests in debug+release timing |
| 11 Fixture masking | P2 onward, gated in P5 | Fresh-DB rule + teardown invariants + negative control |
| 12 Broken peer serving | P4, P5 | Fresh-node-through-pruned-mesh E2E |

**Ordering rationale:** P1 before P2 because the bidirectional map is the safety gate for every erasure decision. P2 before P3 because policy eligibility checks (anchors, spent-ness) call erasure primitives whose semantics (seal survival, PRUNED errors) must already exist. P4 before P5 because survival tests are the preconditions that make the E2E/disk claims honest.

## Sources

**Repo-verified (HIGH confidence):**
- `src/crdt/impl/crdt_datastore.cpp` — root-job creation without resolved-guard (774–818), traversal links-to-fetch flow (820–898), delta processing order merge→addNode→DeleteCIDBlock(LRU only) (1011–1036), heads add/replace + markResolved (2111–2199), DAG worker loop and retry queues (99–135, 118–120)
- `src/crdt/impl/graphsync_dagsyncer.cpp` — TraverseCIDsLinks resolved/local/fetch decision order (683–797), DeleteCIDBlock is LRU-cache-only (855–862)
- `src/crdt/impl/crdt_set.cpp` — PutElems writes every element of a delta under one block id (669–735); PutTombs (749–791)
- `src/transaction/TransactionManager.cpp` — persisted-tx scan for nonce recovery (1602–1649), boot UTXO mounting + missing_tx_hashes_ (3196–3249), QueryTransactions (2473–2503)
- `src/account/UTXOManager.cpp` — `/utxo/` + `/utxo-checkpoint/` keyspaces (18–34), UTXO states incl. CONSUMED→READY revert path (377–390)
- Vendored `ipfs-lite-cpp`: `datastore_rocksdb.cpp` (seal marker is a separate `<cid>sealed` key; remove deletes only the block, 41–82), `ipfs_block_service.cpp`, `merkledag_service_impl.cpp` (`select` returns UNRESOLVED_LINK on missing block, 44–63)
- Repo memories: archive-repl flake root cause (single DAG worker pinned by stalled graphsync fetch; 30 s recovery), cert-receipt deferred-dispatch invariant (pre-commit callbacks; nonce-chain divergence failure signature)
- `.planning/codebase/CONCERNS.md` — CRDT resync/fragility notes, deferred equal-priority overwrite guard (via PROJECT.md STATE), non-atomic UTXO recording

**External (MEDIUM-HIGH confidence, official docs + corroborating sources):**
- [Lotus SplitStore docs](https://lotus.filecoin.io/lotus/configure/splitstore) — concurrent marking, protecting finalities of state + all headers, transactional compaction, snapshot re-bootstrap after pruning; [issue #10699](https://github.com/filecoin-project/lotus/issues/10699) (default-threshold GC pitfalls); [design discussion #5788](https://github.com/filecoin-project/lotus/discussions/5788)
- [Blockstream: pruned node limitations](https://help.blockstream.com/education/nodes/introduction-and-role/should-i-run-a-bitcoin-full-node-or-a-pruned-node) (reorg deeper than retained window impossible); [bitcoin/bitcoin#28684](https://github.com/bitcoin/bitcoin/issues/28684) (prune ⊕ txindex hard incompatibility); [bitcoin-core/gui#709](https://github.com/bitcoin/bitcoin/issues/709) (silent prune re-enable foot-gun)
- [RocksDB: space not reclaimed after delete](https://stackoverflow.com/questions/66543704/rocksdb-is-not-freeing-up-space-after-a-delete) ([GH #8041](https://github.com/facebook/rocksdb/issues/8041)); [DeleteRange blog](http://rocksdb.org/blog/2018/11/21/delete-range.html); [compaction priority blog](http://rocksdb.org/blog/2016/01/29/compaction_pri.html) — tombstones reclaim space only at bottommost-level compaction; no-write interlock
- [ScyllaDB: Preventing Data Resurrection with Repair-Based Tombstone GC](https://www.scylladb.com/2022/06/30/preventing-data-resurrection-with-repair-based-tombstone-garbage-collection); [safe tombstone GC](https://abstractopedia.org/mechanisms/safe_tombstone_garbage_collection) ("only once every replica has surely seen the delete"); [CRDT survey pt. 4 — causal stability for GC](https://mattweidner.com/2023/09/26/crdt-survey-4.html); [Riak CRDT history](https://christophermeiklejohn.com/erlang/lasp/2019/03/08/monotonicity.html)
- [go-graphsync architecture](https://github.com/ipfs/go-graphsync/blob/main/docs/architecture.md) and [graphsync spec](https://ipld.io/specs/transport/graphsync) — responder traversal fails on unresolvable links (RequestFailed/Partial semantics)
- [Solana getTransaction](https://solana.com/docs/rpc/http/gettransaction) (null ambiguity) vs [OnFinality: explicit ledger-pruning errors](https://onfinality.io/en/learn/solana-historical-data-rpc)

---
*Pitfalls research for: SuperGNUS v1.2 light-node transaction pruning*
*Researched: 2026-10-15*
