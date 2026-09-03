---
phase: 12-multi-node-finality-fault-proof
reviewed: 2026-09-03T11:36:42Z
depth: standard
files_reviewed: 4
files_reviewed_list:
  - test/src/blockchain/multi_node_finality_fault_test.cpp
  - test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp
  - test/testutil/storage/base_crdt_test.cpp
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
findings:
  critical: 1
  warning: 2
  info: 7
  total: 10
status: issues_found
---

# Phase 12: Code Review Report (round 4 — supersedes commit b4dbdc91)

**Reviewed:** 2026-09-03T11:36:42Z
**Depth:** standard
**Files Reviewed:** 4
**Status:** issues_found

## Summary

This round covers the three round-3 gap-closure commits against the current state of the
four files: 60e449bb (SameBurn first-wait predicate), 414ca5ea (teardown order in the
compatibility smoke test and `~CRDTFixture`), and f6f63b96 (teardown order in the two
`consensus_pending_lifecycle_test.cpp` loops).

Verified correct:

- **60e449bb (SameBurn first wait, `multi_node_finality_fault_test.cpp:2088-2096`):** the
  predicate now requires `HasBridgeMarker` on all four peers, exactly mirroring the
  post-restart wait (lines 2106-2115). The rationale is independently confirmed in
  production source: `++mint_effects_for_test_` (`TransactionManager.cpp:5517-5519`)
  executes before `PersistBridgeExecutedMarker` (`:5532`) on the same path, so the old
  predicate could observe `MintEffects == 1` while the marker was still in flight and the
  immediately-following `HasBridgeMarker` assertion (line 2100) could fail spuriously.
  The added terms are null-safe (`HasBridgeMarker` checks `peer.db`), boolean, and polled
  at the macro's 10 ms interval inside the unchanged 20 s bound. The check-then-act gap is
  closed.
- **414ca5ea in the smoke test (CR-01): resolved.** `ComponentPeer::Stop`
  (`multi_node_finality_fault_compatibility_smoke_test.cpp:265-292`) now releases
  `db`/`account` (lines 287-288) after `io->stop()`/thread join and before
  `pubsub->Stop()` (line 289), mirroring `Peer::Stop`. Every GlobalDB co-owner inside the
  struct (`transactions`, `blockchain` and its `consensus_manager_` member, `consensus`)
  is reset earlier (lines 267-271), so at `pubsub->Stop()` the GlobalDB graphsync chain no
  longer co-owns the libp2p host. The smoke test's omission of an explicit
  `consensus->Close()` (unlike `Peer::Stop`) is safe: `~ConsensusManager()` calls
  `Close()` itself (`Consensus.cpp:149-152`), which joins `round_timer_`.
- **414ca5ea in `~CRDTFixture` (CR-02): resolved.** `db_.reset()` is now the first
  release (`base_crdt_test.cpp:122`), before `pubs_->Stop()` (line 125) and
  `io_.reset()` (line 128); the `fs::remove_all` cleanup block is unchanged and last.
  Derived test bodies hold no GlobalDB refs at fixture-destruction time (their
  managers/registries are function-locals destroyed before the fixture).

The dominant remaining problem: **f6f63b96 does not actually fix WR-01.** The new
teardown loops reset `manager`/`db`/`account` but never `node.registry`, and
`ValidatorRegistry` retains `std::shared_ptr<crdt::GlobalDB> db_`
(`ValidatorRegistry.hpp:530`, initialized by `db_( std::move( db ) )` at
`ValidatorRegistry.cpp:107`). The GlobalDB therefore still survives
`node.pubsub->Stop()` until the `nodes` array is destroyed at scope exit — the 12-14
teardown SIGSEGV window the commit claims to close is unchanged (see CR-03).

## Prior-Round Findings — Status Against Current Source

| Prior ID | Location | Status |
|----------|----------|--------|
| CR-01 | smoke test `ComponentPeer::Stop` order | **RESOLVED** (414ca5ea) — verified above |
| CR-02 | `~CRDTFixture` order | **RESOLVED** (414ca5ea) — verified above |
| WR-01 | lifecycle teardown loops | **NOT RESOLVED** — f6f63b96 is ineffective; escalated to **CR-03** below |
| WR-02 | `TransactionManager::Stop` / `ConsensusManager::Close` notify without paired mutex | **OPEN (by decision)** — still present (`TransactionManager.cpp:314-323`, `Consensus.cpp:155-159`); the repair was deliberately withheld by the 12-15 authorization gate (recorded in 60e449bb's commit message); src files are outside this round's file scope |
| WR-03 | CMake `APPLE`-only preflight | **OPEN** — `test/src/blockchain/CMakeLists.txt:62-63` still hard-fails on non-Apple despite the per-primitive `P12_HAVE_*` checks that follow; file out of this round's scope, re-verified only for existence |
| WR-04 | fixed port 40001 | **OPEN** — see WR-04 below, refined with current CTest properties |
| IN-01 | `fcntl` on closed fds in collector child | **OPEN** — unchanged at `multi_node_finality_fault_test.cpp:1551-1554` |
| IN-02 | fixture reap failures swallowed | **OPEN** — unchanged at `base_crdt_test.cpp:66-80` |
| IN-03 | stale "(10 seconds)" comment on a 5000 ms constant | **OPEN** — `TransactionManager.hpp:50-51` (src, out of file scope; re-verified) |
| IN-04 | dead `GNUS_FULL_NODES_TOPIC_LEGACY` | **OPEN** — `TransactionManager.hpp:49`, still unreferenced (src, out of file scope; re-verified) |
| IN-05 | unsynchronized `Configure*` writes | **OPEN** — `Consensus.cpp:469-510` still writes `round_duration_`/`round_skew_`/`certificate_delay_` without `timer_mutex_` (src, out of file scope; re-verified) |

## Critical Issues

### CR-03: f6f63b96's WR-01 fix is ineffective — `node.registry` keeps the GlobalDB alive across `pubsub->Stop()`, so the 12-14 teardown crash window remains open

**File:** `test/src/blockchain/consensus_pending_lifecycle_test.cpp:1326-1336` and `2027-2037`
**Issue:** Both new teardown loops do:

```cpp
sgns::ConsensusPendingLifecycleTestAccess::Close( node.manager );
node.manager.reset();
node.db.reset();
node.account.reset();
node.pubsub->Stop();
```

`MultiValidatorNode::registry` (declared at `:633`, populated by `MakeMultiValidatorRegistry`
at `:1199` / `:1903`) is never reset, and `ValidatorRegistry` stores the GlobalDB by value
(`std::shared_ptr<crdt::GlobalDB> db_;` — `ValidatorRegistry.hpp:530`; ctor
`ValidatorRegistry.cpp:107`). `Close(manager)` only stops/joins the round timer
(`Consensus.cpp:155-184`) — it does not release the manager's or registry's members. The
net effect on object lifetimes: before the fix the GlobalDB was alive at
`pubsub->Stop()` via `{node.db, node.registry, manager}`; after the fix it is still alive
via `{node.registry}`. The commit therefore does not change the GlobalDB lifetime at all.

Consequently `node.pubsub->Stop()` (`StopImpl`) still destroys `m_host`/`m_context` while
the GlobalDB graphsync chain (`GlobalDB -> CrdtDatastore -> GraphsyncDAGSyncer ->
GraphsyncImpl -> graphsync::Network::host_`) co-owns the libp2p host. The last host
co-owner is released only when `node.registry` is destroyed at `nodes` scope exit — after
`Stop()` has already freed `m_context` — and the resulting `~BasicHost` deregisters the
leftover self-dial `TcpConnection` (each node starts with
`Start( 54001 + index, { GetLocalAddress() } )`, `:657-659`) from the freed kqueue
reactor. This is the exact teardown SIGSEGV mechanism the loops' own new comments claim
to prevent, in the same self-connection-only profile that CR-02 was graded Critical for.
The comment added by the commit ("so StopImpl is the final libp2p host release") is
factually false at both sites.
**Fix:** Reset the registry inside both loops before `pubsub->Stop()`:

```cpp
for ( auto &node : nodes )
{
    sgns::ConsensusPendingLifecycleTestAccess::Close( node.manager );
    node.manager.reset();
    node.registry.reset(); // registry co-owns GlobalDB (ValidatorRegistry.hpp:530)
    node.db.reset();
    node.account.reset();
    node.pubsub->Stop();
}
```

## Warnings

### WR-04: Fixed listen port 40001 still shared by every CRDTFixture user; `consensus_pending_lifecycle_test` has no serialization properties

**File:** `test/testutil/storage/base_crdt_test.cpp:102`
**Issue:** Every `CRDTFixture` still binds the fixed port
`pubs_->Start( 40001, { pubs_->GetLocalAddress() } )`. Current CTest properties sharpen
the exposure: `multi_node_finality_fault_compatibility_smoke_test` is protected
(`RUN_SERIAL TRUE`, `CMakeLists.txt:57`) and the multi-node targets hold
`RUN_SERIAL TRUE` + `RESOURCE_LOCK phase12_real_socket_ports` (`CMakeLists.txt:122-123,
134-135`), but `consensus_pending_lifecycle_test` (`CMakeLists.txt:38`) has neither, and
neither do the other CRDTFixture-derived binaries. Any two of them overlapping in a
parallel ctest run contend for 40001; the loser's `Start(...).get()` returns an error
whose only handling is `BOOST_ASSERT_MSG` (`base_crdt_test.cpp:104`) — a no-op in NDEBUG
builds, so the fixture proceeds with a dead transport. The sibling scheme in this round's
files (`static_cast<uint16_t>( 54001U + index )`, `consensus_pending_lifecycle_test.cpp:657`,
with `+10` offsets in the second test) is the same fixed-port class across repeated runs.
**Fix:** Bind port 0 and read back the assigned ephemeral port from the host, or derive a
per-fixture port; alternatively give every CRDTFixture-based CTest target
`RUN_SERIAL TRUE` or a shared `RESOURCE_LOCK crdt_fixture_port` (at minimum
`consensus_pending_lifecycle_test`).

### WR-05: Transport-start failures are non-fatal — tests continue wiring and dereferencing an unstarted pubsub

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1006`;
`test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp:321`
**Issue:** Both `StartPeer` helpers use
`EXPECT_FALSE( peer.pubsub->Start( port, ... ).get() );` and then fall through to
`peer.pubsub->GetHost()` (graphsync wiring at `:1010` / `:326`) and later `ConnectPeers`.
`EXPECT_FALSE` is non-fatal: when the listener cannot bind, the test keeps executing
against an unstarted transport, converting a clean, attributable failure into a null-host
dereference far from the root cause — the flaky-crash signature this phase has been
eliminating. The sibling helper in this round's files already shows the correct pattern
(`consensus_pending_lifecycle_test.cpp:657-664`): capture the result, `ADD_FAILURE`, and
return the partial node so the caller's `ASSERT_TRUE(...)` aborts cleanly.
**Fix:**

```cpp
const auto start_result = peer.pubsub->Start( port, { peer.pubsub->GetLocalAddress() } ).get();
EXPECT_FALSE( start_result );
if ( start_result )
{
    return peer; // transport down: stop here, let the caller's ASSERT_TRUE fail the test
}
```

## Info

### IN-01: `fcntl(F_SETFD)` on already-closed descriptors in the evidence-collector child

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1551-1554`
**Issue:** The child closes `data_pipe[0]`/`control_pipe[0]` and immediately calls
`::fcntl(..., F_SETFD, FD_CLOEXEC)` on the just-closed descriptors — guaranteed `EBADF`
no-ops; the intended hygiene is already provided by the subsequent `dup2`/`close` and the
explicit `flags & ~FD_CLOEXEC` on `control_pipe[1]`.
**Fix:** Delete the two dead `fcntl` calls.

### IN-02: Fixture reap failures are swallowed, silently reintroducing the poisoning the reap targets

**File:** `test/testutil/storage/base_crdt_test.cpp:66-80`
**Issue:** If `fs::remove_all` throws during the construction-time reap, the handler
prints to stderr and construction continues to open the very database the reap was meant
to delete — the stale-state failure mode 12-13 set out to eliminate, now with only a
stderr hint.
**Fix:** Fail the fixture loudly on reap error (throw / `GTEST_FAIL`), or retry once
before proceeding.

### IN-03: Stale doc comment on `NONCE_REQUEST_TIMEOUT_MS` (src, out of file scope)

**File:** `src/account/TransactionManager.hpp:50-51`
**Issue:** The constant is `5000` ms but the comment says "(10 seconds)". Re-verified
unchanged this round.
**Fix:** Update the comment to "(5 seconds)".

### IN-04: Dead legacy topic constant (src, out of file scope)

**File:** `src/account/TransactionManager.hpp:49`
**Issue:** `GNUS_FULL_NODES_TOPIC_LEGACY` still has no references beyond its declaration.
**Fix:** Remove it, or wire it into the legacy-topic fallback if that behavior is still
intended.

### IN-05: Consensus round-duration config writes are unsynchronized with the timer-thread read (src, out of file scope)

**File:** `src/blockchain/Consensus.cpp:469-510`
**Issue:** `ConfigureTimestampWindow`/`ConfigureRoundDuration`/`ConfigureRoundSkew`/
`ConfigureCertificateDelay` write the `std::chrono::milliseconds` members without
`timer_mutex_` while the round timer reads them under the mutex. Latent data race (no
current production callers). Re-verified unchanged this round.
**Fix:** Take `std::lock_guard lock( timer_mutex_ )` in the setters, or make the
durations atomics.

### IN-06: Pipe-descriptor leak when the second `pipe()` fails in the evidence collector

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1539-1542`
**Issue:** `if ( ::pipe( data_pipe ) != 0 || ::pipe( control_pipe ) != 0 ) return Invalid(...)`
— if `control_pipe` fails after `data_pipe` succeeded, `data_pipe[0]`/`data_pipe[1]` are
never closed; both were also already set non-blocking. Rare error path, leaks two fds for
the remainder of the test-binary process.
**Fix:**

```cpp
if ( ::pipe( data_pipe ) != 0 || ::pipe( control_pipe ) != 0 )
{
    if ( data_pipe[0] ) { ::close( data_pipe[0] ); ::close( data_pipe[1] ); }
    if ( control_pipe[0] ) { ::close( control_pipe[0] ); ::close( control_pipe[1] ); }
    return Invalid( filter, run_token, "pipe-unavailable" );
}
```

### IN-07: Keypair failure in the fixture constructor hard-crashes instead of failing the test

**File:** `test/testutil/storage/base_crdt_test.cpp:84`
**Issue:** `std::make_shared<GossipPubSub>( KeyPairFileStorage( keypair_path_ ).GetKeyPair().value() )`
— if `GetKeyPair` returns an error, `.value()` on the errored outcome throws/terminates
rather than producing a gtest failure, and the `BOOST_ASSERT_MSG( pubs_ != nullptr, ...)`
on line 86 is unreachable in that path (and a no-op under NDEBUG).
**Fix:** Check the result and fail the fixture with a diagnostic before calling `.value()`
(e.g. store the result, `if ( keypair.has_error() ) { GTEST_FAIL() ...; }` or throw a
descriptive exception).

---

_Reviewed: 2026-09-03T11:36:42Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
