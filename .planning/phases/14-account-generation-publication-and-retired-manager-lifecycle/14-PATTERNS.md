# Phase 14: Account-generation publication and retired-manager lifecycle safety - Pattern Map

**Mapped:** 2026-08-18
**Files analyzed:** 6 likely modified files
**Analogs found:** 6 / 6 (in-place analogs; two required contracts have only partial analogs)

## Scope and File Inventory

Phase 14 should modify the existing account lifecycle surfaces in place. No new subsystem is indicated by `14-CONTEXT.md`, `14-RESEARCH.md`, or `14-VALIDATION.md`, and the existing test targets already compile the two scoped test files.

Likely modified files:

- `src/account/GeniusNode.hpp`
- `src/account/GeniusNode.cpp`
- `src/account/TransactionManager.hpp`
- `src/account/TransactionManager.cpp`
- `test/src/multiaccount/multi_account_sync.cpp`
- `test/src/account/account_management_test.cpp`

Do not expand the plan into bridge owner redesign, trusted-peer refresh/timer work, or repository-wide `AtomicTransaction`/GlobalDB/CRDT authority changes. `test/src/multiaccount/CMakeLists.txt` and `test/src/account/CMakeLists.txt` need no change unless tests are split into new source files; the upstream artifacts call for extending the existing files.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/account/GeniusNode.hpp` | provider / model / config | request-response + event-driven | its existing `AccountServiceSnapshot`, lifecycle fields, public `outcome::result` APIs | exact role; lifecycle model must be expanded |
| `src/account/GeniusNode.cpp` | service / lifecycle controller | event-driven + request-response | its generation/owner callback guards and lifecycle-locked snapshots | exact role and flow |
| `src/account/TransactionManager.hpp` | service contract / model | event-driven + CRUD | its `PendingTransactionWait`, typed completion, and Outcome error enum | exact role; admission/retirement model absent |
| `src/account/TransactionManager.cpp` | service | event-driven + CRUD | its async wait ledger, terminal notification, idempotent completion, and mutation entry points | exact role and flow |
| `test/src/multiaccount/multi_account_sync.cpp` | integration / deterministic concurrency test | event-driven | its `MultiAccountTestAccess`, condition-variable barrier, and generation consistency test | exact |
| `test/src/account/account_management_test.cpp` | public API integration test | request-response + event-driven wait | its account selection acceptance/readiness tests and fixture | exact |

## Pattern Assignments

### `src/account/GeniusNode.hpp` (provider/model/config, request-response + event-driven)

**Primary analog:** `src/account/GeniusNode.hpp`

Use the current snapshot as the structural seed for one coherent published generation, but expand it to a lifecycle snapshot or ready bundle that also owns processing state. The key convention is that related owners and their generation are copied together, not reconstructed from individual fields.

**Coherent owner model** (`src/account/GeniusNode.hpp:788-798`):

```cpp
/**
 * One published account generation.  The two owners and their generation
 * are copied together while holding lifecycle_mutex_; callers must not
 * reconstruct this tuple from the individual member shared_ptrs.
 */
struct AccountServiceSnapshot
{
    std::shared_ptr<GeniusAccount>      account;
    std::shared_ptr<TransactionManager> manager;
    uint64_t                            generation = 0;
};
```

Copy this value-object pattern into explicit `ready`, `retiring`, and `pending` generation records. The visible snapshot must be either a complete ready bundle or empty; never expose a pending account with no ready manager/processing owner.

**One lifecycle synchronization domain** (`src/account/GeniusNode.hpp:819-835`):

```cpp
std::shared_ptr<TransactionManager> transaction_manager_;
/// Serializes lifecycle work while permitting the existing synchronous nested transitions.
mutable std::recursive_mutex lifecycle_mutex_;
/// Published account/manager epoch.  A switching epoch is intentionally unavailable.
uint64_t account_service_generation_ = 0;
bool     account_service_switching_  = false;
/// State currently executing inside StateTransition; nested transitions temporarily replace it.
std::optional<NodeState> transition_in_progress_;
/// Monotonic accepted-transition epoch used to invalidate stale posted lifecycle callbacks.
uint64_t transition_epoch_ = 0;
```

Keep generation allocation, lifecycle state, and all ready/pending/retiring bundle moves under this mutex. Replace the boolean with an explicit lifecycle state capable of distinguishing `SWITCHING`, `READY`, and `ACCOUNT_UNAVAILABLE`. The mutex is the publication lock, not a drain-wait lock.

**Typed public error convention** (`src/account/GeniusNode.hpp:197-218`):

```cpp
enum class Error : uint8_t
{
    INSUFFICIENT_FUNDS        = 1,
    // ...
    TRANSACTIONS_NOT_READY    = 13,
    TRANSACTION_NOT_FINALIZED = 14,
    TRANSACTION_FAILED        = 15,
    INVALID_NODE_TYPE         = 16,
};
```

Add distinct `SWITCH_IN_PROGRESS` and `ACCOUNT_UNAVAILABLE` enumerators here and continue returning `outcome::result<T>` from fallible APIs. `SelectAccount` currently uses that convention (`src/account/GeniusNode.hpp:296-301`) but should return an acceptance value containing the switch generation, not `void`.

**Lifecycle-specific status surface to replace** (`src/account/GeniusNode.hpp:531-540`):

```cpp
[[nodiscard]] processing::ProcessingServiceImpl::ProcessingStatus GetProcessingStatus() const
{
    return processing_service_ == nullptr ? processing::ProcessingServiceImpl::ProcessingStatus(
                                                processing::ProcessingServiceImpl::Status::DISABLED,
                                                0.0f )
                                          : processing_service_->GetProcessingStatus();
}
```

Do not copy this unsynchronized inline dereference. Change the signature to a typed lifecycle result/snapshot and implement it through the same lifecycle-locked ready bundle used by account and manager access. While switching return `SWITCH_IN_PROGRESS`; after failed initialization return `ACCOUNT_UNAVAILABLE`; query processing only from a ready replacement generation.

---

### `src/account/GeniusNode.cpp` (service/lifecycle controller, event-driven + request-response)

**Primary analog:** generation-checked callbacks and lifecycle-locked snapshot helpers already in `src/account/GeniusNode.cpp`.

**Central transition publication under the lifecycle mutex** (`src/account/GeniusNode.cpp:630-644`):

```cpp
void GeniusNode::StateTransition( NodeState next_state )
{
    std::lock_guard<std::recursive_mutex> lifecycle_lock( lifecycle_mutex_ );
    if ( transition_in_progress_.has_value() && transition_in_progress_.value() == next_state )
    {
        return;
    }

    const auto previous_transition = transition_in_progress_;
    transition_in_progress_        = next_state;
    ++transition_epoch_;
    state_.store( next_state );
```

Copy the single-lock commit shape for account lifecycle publication: update lifecycle state, accepted generation, and the complete visible bundle in one critical section. Prepare event payloads under the lock, but invoke callbacks after releasing it.

**Current coherent snapshot/revalidation helper** (`src/account/GeniusNode.cpp:3387-3407`):

```cpp
GeniusNode::AccountServiceSnapshot GeniusNode::SnapshotAccountServices() const
{
    std::lock_guard<std::recursive_mutex> lifecycle_lock( lifecycle_mutex_ );
    if ( account_service_switching_ )
    {
        return {};
    }
    return { account_, transaction_manager_, account_service_generation_ };
}

bool GeniusNode::ApplyIfCurrentAccountServices( const AccountServiceSnapshot &snapshot,
                                                const std::function<void()>  &side_effect )
{
    std::lock_guard<std::recursive_mutex> lifecycle_lock( lifecycle_mutex_ );
    if ( account_service_switching_ || snapshot.generation != account_service_generation_ ||
         snapshot.account.get() != account_.get() || snapshot.manager.get() != transaction_manager_.get() )
    {
        return false;
    }
    side_effect();
    return true;
}
```

Use this as the exact comparison pattern for generation plus owner identity. Extend it so a ready snapshot includes processing ownership and explicit lifecycle state. Avoid running arbitrary/user side effects under the lock; commit/copy under lock and dispatch after unlock.

**Generation plus expected-owner callback guard** (`src/account/GeniusNode.cpp:1030-1046`):

```cpp
transaction_manager_->RegisterStateChangeCallback(
    [weak_self = weak_from_this(),
     weak_manager = std::weak_ptr<TransactionManager>( manager ),
     owner_generation]( TransactionManager::State old_state, TransactionManager::State new_state )
    {
        if ( auto strong = weak_self.lock() )
        {
            auto callback_manager = weak_manager.lock();
            const auto snapshot = strong->SnapshotAccountServices();
            if ( !callback_manager || snapshot.generation != owner_generation ||
                 snapshot.manager.get() != callback_manager.get() )
            {
                return;
            }
            strong->TransactionStateChanged( old_state, new_state );
        }
    } );
```

Apply this pattern to blockchain start completion, posted transaction initialization, manager readiness, processing readiness, and failure cleanup. Each callback captures generation `G` and the expected pending owner; it must recheck both before advancing or publishing.

**Posted stale-work rejection** (`src/account/GeniusNode.cpp:890-928`):

```cpp
{
    std::lock_guard<std::recursive_mutex> lifecycle_lock( self->lifecycle_mutex_ );
    source_state = self->state_.load();
    captured_epoch = self->transition_epoch_;
}

boost::asio::post(
    *self->io_,
    [weak_self, captured_epoch, source_state, target_state]
    {
        auto node = weak_self.lock();
        if ( !node ) return;

        std::lock_guard<std::recursive_mutex> lifecycle_lock( node->lifecycle_mutex_ );
        if ( node->transition_epoch_ != captured_epoch ||
             node->state_.load() != source_state )
        {
            return;
        }
        node->StateTransition( target_state );
    } );
```

This is the best in-tree analog for stale callback rejection. Account-switch callbacks must use the accepted account generation rather than relying only on node state, because two generations can visit the same state.

**Strong ownership across blocking async work plus stale check** (`src/account/GeniusNode.cpp:3768-3808`):

```cpp
const auto generation = account_services.generation;
auto       tx_mgr     = account_services.manager;
auto       provider   = rpc_endpoint_provider_;
auto       relayer    = bridge_relayer_;
boost::asio::post( *io_,
                   [weak_self = weak_from_this(), generation,
                    tx_mgr = std::move( tx_mgr ), provider = std::move( provider ),
                    relayer = std::move( relayer )]() mutable
                   {
                       auto strong = weak_self.lock();
                       if ( !strong || strong->SnapshotAccountServices().generation != generation )
                       {
                           return;
                       }
                       // ...
                       auto is_cancelled = [weak_self, generation]() -> bool
                       {
                           auto s = weak_self.lock();
                           return !s || s->SnapshotAccountServices().generation != generation;
                       };
                       provider->Initialize( config_path, validator, is_cancelled );
                   } );
```

Reuse only the ownership/generation-check shape; bridge ownership itself is outside Phase 14. The retiring runtime must stay strongly owned while admitted operations terminalize, and pending owners must stay private until the one ready commit.

**Acceptance boundary and blocking-work split** (`src/account/GeniusNode.cpp:2516-2539`):

```cpp
{
    std::lock_guard<std::recursive_mutex> lifecycle_lock( lifecycle_mutex_ );
    if ( account_service_switching_ )
    {
        return std::errc::operation_in_progress;
    }
    account_service_switching_ = true;
    ++account_service_generation_;
    previous_watcher = std::move( catchup_watcher_ );
}

// ... Stop may block or join threads; it deliberately runs without lifecycle_mutex_.
auto shutdown_result = ShutdownAccountBoundServices( true );
```

Preserve the lock split, but change the work sequence: synchronously close manager admission inside the acceptance critical section, return the typed acceptance promptly, then asynchronously wait for drain without holding `lifecycle_mutex_`. Replacement initialization starts only after real drain. The existing early `account_` publication and switching clears at `src/account/GeniusNode.cpp:2557-2567` are anti-patterns and must not be copied.

**Typed error category implementation** (`src/account/GeniusNode.cpp:146-184`):

```cpp
OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, GeniusNode::Error, e )
{
    switch ( e )
    {
        // ...
        case sgns::GeniusNode::Error::TRANSACTIONS_NOT_READY:
            return "Transaction manager is not ready";
        // ...
    }
    return "Unknown error";
}
```

Add stable messages for the new lifecycle errors here. Replace the current `GetAddress()` sentinel (`src/account/GeniusNode.cpp:3410-3418`) with a typed result that maps the coherent lifecycle snapshot.

---

### `src/account/TransactionManager.hpp` (service contract/model, event-driven + CRUD)

**Primary analog:** the manager's typed completion/wait model.

**Typed manager error and terminal completion model** (`src/account/TransactionManager.hpp:66-113`):

```cpp
enum class Error : uint8_t
{
    TRUST_POLICY_NOT_READY = 1,
};

enum class TransactionStatus : uint8_t
{
    CREATED,
    SENDING,
    CONFIRMED,
    VERIFYING,
    UNCONFIRMED,
    FAILED,
    INVALID
};

struct TransactionCompletion
{
    std::string               transaction_id;
    TransactionStatus         status{ TransactionStatus::INVALID };
    std::chrono::milliseconds elapsed{};
    boost::system::error_code error;
};
```

Add `MANAGER_RETIRED` to the existing error enum. Extend the terminal event/diagnostic value with the immutable manager generation so accepted old-generation completions cannot be attributed to a replacement. Keep error transport typed; do not reuse `operation_aborted` to mean retirement.

**Idempotent async record shape** (`src/account/TransactionManager.hpp:349-367`):

```cpp
struct PendingTransactionWait
{
    // ...
    boost::asio::steady_timer             timer;
    std::string                           tx_id;
    TransactionCompletionCallback         callback;
    std::chrono::steady_clock::time_point started_at;
    std::atomic_bool                      completed{ false };
};
```

This is the closest model analog for admitted-operation records. Introduce a manager lifecycle enum (`ACTIVE`, `DRAINING`, `RETIRED`), a manager generation, an admitted-operation ledger, and an immutable retirement snapshot. The admitted record must live until terminal outcome, not merely until the synchronous mutator returns.

**Existing terminal helper boundary** (`src/account/TransactionManager.hpp:534-541`):

```cpp
TransactionStatus GetStatusByTxId( const std::string &txId, std::optional<bool> outgoing ) const;
bool              SetOutgoingStatusByNonce( uint64_t nonce, TransactionStatus s );
static bool       IsTerminalTransactionStatus( TransactionStatus status );
void              NotifyTransactionStatusChanged( const std::string &tx_id );
void              CompleteTransactionWait( const std::shared_ptr<PendingTransactionWait> &wait,
                                           TransactionStatus status,
                                           boost::system::error_code error = {} );
```

Make one canonical terminal predicate serve state transitions, async waits, admission-ledger completion, drain-zero notification, and frozen diagnostics. Completion must remain idempotent.

**Current synchronization fields to colocate, not overload** (`src/account/TransactionManager.hpp:587-602`):

```cpp
mutable std::mutex          mutex_m;
std::deque<TransactionItem> tx_queue_m;
// ...
std::atomic<bool> stopped_{ false };
std::mutex payout_submission_mutex_;
std::mutex transaction_waits_mutex_;
std::unordered_map<std::string, std::vector<std::shared_ptr<PendingTransactionWait>>> transaction_waits_;
```

Add a dedicated admission mutex/state/ledger. `stopped_`, the queue mutex, and `payout_submission_mutex_` are not substitutes for the D-05 linearization point. `TryAdmit()` and `CloseAdmission()` must serialize on the same mutex.

---

### `src/account/TransactionManager.cpp` (service, event-driven + CRUD)

**Primary analog:** async transaction waits and transaction-state terminal notification.

**Typed Outcome category** (`src/account/TransactionManager.cpp:37-45`):

```cpp
OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, TransactionManager::Error, e )
{
    switch ( e )
    {
        case sgns::TransactionManager::Error::TRUST_POLICY_NOT_READY:
            return "TRUST_POLICY_NOT_READY";
    }
    return "Unknown TransactionManager error";
}
```

Add an explicit `MANAGER_RETIRED` case here and return it from every external mutation entry point after admission closes.

**Mutation boundary requiring one admission token** (`src/account/TransactionManager.cpp:571-593`):

```cpp
outcome::result<std::string> TransactionManager::TransferFunds( ... )
{
    if ( GetState() != State::READY )
    {
        return outcome::failure( boost::system::error_code{} );
    }
    BOOST_OUTCOME_TRY( auto params,
        account_m->GetUTXOManager().CreateTxParameter( amount, std::move( destination ), token_id ) );
    auto transfer_transaction = std::make_shared<TransferTransaction>(
        TransferTransaction::New( inputs, outputs, FillDAGStruct() ) );
    transfer_transaction->MakeSignature( *account_m );
    account_m->GetUTXOManager().ReserveUTXOs( inputs, transfer_transaction->GetHash() );
    EnqueueTransaction( std::make_pair( transfer_transaction, std::nullopt ) );
    return transfer_transaction->GetHash();
}
```

Insert `TryAdmit()` before `CreateTxParameter`/`FillDAGStruct` and carry the resulting move-only operation token through reservation and enqueue. Apply the same pattern to mint, migration, escrow hold/pay, async pay, and direct enqueue. Do not reacquire admission at enqueue: an operation admitted before close must be allowed to finish.

**Existing enqueue serialization** (`src/account/TransactionManager.cpp:1120-1143`):

```cpp
void TransactionManager::EnqueueTransaction( TransactionItem element )
{
    // ...
    for ( auto &&[tx, _] : element.first )
    {
        auto result = ChangeTransactionState( tx, TransactionStatus::CREATED );
        // ...
    }
    std::lock_guard lock( mutex_m );
    tx_queue_m.emplace_back( std::move( element ) );
}
```

Change enqueue to accept the already-issued admission token and return `outcome::result<void>`. The queue mutex remains the queue serialization point; admission is a preceding ownership decision.

**Track asynchronous work to exactly one terminal completion** (`src/account/TransactionManager.cpp:950-1017`):

```cpp
auto wait = std::make_shared<PendingTransactionWait>( *ctx_m, std::move( tx_id ),
                                                      std::move( callback ),
                                                      std::chrono::steady_clock::now() );
wait->timer.expires_after( timeout );
{
    std::lock_guard lock( transaction_waits_mutex_ );
    if ( stopped_.load() )
        wait->completed.store( true );
    else
        transaction_waits_[wait->tx_id].push_back( wait );
}
// ... weak owner + weak wait timer callback ...
const auto status = GetOutgoingStatusByTxId( wait->tx_id );
if ( IsTerminalTransactionStatus( status ) )
{
    CompleteTransactionWait( wait, status );
}
```

The admitted-operation ledger should copy this register-before-recheck shape to avoid missing a concurrent terminal transition. Unlike the observer timeout, a switch drain timeout must not mark an admitted durable operation complete or cancelled.

**Idempotent completion, removal under lock, callback after lock** (`src/account/TransactionManager.cpp:1047-1085`):

```cpp
if ( wait->completed.exchange( true ) )
{
    return;
}
wait->timer.cancel( ignored );
{
    std::lock_guard lock( transaction_waits_mutex_ );
    // remove the completed record
}
auto callback = std::move( wait->callback );
TransactionCompletion completion{ /* terminal value */ };
boost::asio::post( *ctx_m,
                   [callback = std::move( callback ), completion = std::move( completion )]() mutable
                   { callback( std::move( completion ) ); } );
```

Copy this ordering for operation terminalization and drain-zero callbacks: make completion idempotent, mutate the admitted set under the admission mutex, copy callbacks/terminal values, unlock, then dispatch. Add the manager generation to the completion value before dispatch.

**Terminal notification after state locks are released** (`src/account/TransactionManager.cpp:4908-4915`):

```cpp
// Notify after every lock local to the transition has been released. Querying
// the tracked value also avoids reporting a requested transition that was rejected.
NotifyTransactionStatusChanged( tx->GetHash() );
return outcome::success();
```

This is the correct convergence point for decrementing the admitted ledger and freezing terminal results. Ensure the canonical terminal set handles `INVALID` consistently; current `IsTerminalTransactionStatus` at lines 1014-1017 omits it even though `ChangeTransactionState` handles it with failure.

**Stop behavior is an anti-analog** (`src/account/TransactionManager.cpp:343-357`):

```cpp
if ( stopped_.exchange( true ) )
{
    return;
}
{
    std::lock_guard submission_lock( payout_submission_mutex_ );
}
CancelPendingTransactionWaits();
cv_.notify_all();
```

Do not call this at account-switch acceptance. It cancels pending observers and stops continuations needed to reach terminal results. The Phase 14 sequence is `CloseAdmission()` (nonblocking) -> wait for admitted ledger to drain -> freeze diagnostics/finalize retirement -> destructive `Stop()` and release. On drain timeout, fail the switch unavailable while keeping durable old work alive; do not claim cancellation.

---

### `test/src/multiaccount/multi_account_sync.cpp` (deterministic concurrency/integration test, event-driven)

**Primary analog:** `MultiAccountTestAccess` and the existing account-generation race fixture.

**Friend-access snapshot/injection pattern** (`test/src/multiaccount/multi_account_sync.cpp:57-90`):

```cpp
class MultiAccountTestAccess
{
public:
    struct AccountGenerationSnapshot
    {
        std::shared_ptr<GeniusAccount>      account;
        std::shared_ptr<TransactionManager> manager;
        std::string                         account_address;
        std::string                         manager_address;
        uint64_t                            generation = 0;
        uint64_t                            catchup_generation = 0;
    };

    static AccountGenerationSnapshot SnapshotAccountGeneration( const std::shared_ptr<GeniusNode> &node )
    {
        auto snapshot = node->SnapshotAccountServices();
        return { snapshot.account, snapshot.manager, /* addresses */, snapshot.generation,
                 node->catchup_callback_owner_generation_.load() };
    }

    static uint64_t InjectCatchupCallback( ... )
    {
        GeniusNode::AccountServiceSnapshot captured{ snapshot.account, snapshot.manager, snapshot.generation };
        node->ApplyIfCurrentAccountServices( captured, [&] { ++side_effects; } );
        return node->catchup_callback_owner_generation_.load();
    }
};
```

Extend this existing test seam with default-empty hooks and inspection for admission, reservation, enqueue, drain-zero, timeout firing, stale completion, ready publication, failure cleanup, retirement snapshot, and processing lifecycle. Keep production behavior unchanged when hooks are unset.

**Deterministic condition-variable barrier** (`test/src/multiaccount/multi_account_sync.cpp:618-634`):

```cpp
std::mutex              selection_barrier_mutex;
std::condition_variable selection_barrier_condition;
size_t                  selection_barrier = 0;
bool                    selection_barrier_open = false;
auto arrive_at_selection_barrier = [&]
{
    std::unique_lock<std::mutex> lock( selection_barrier_mutex );
    if ( ++selection_barrier == 3 )
    {
        selection_barrier_open = true;
        selection_barrier_condition.notify_all();
    }
    else
    {
        selection_barrier_condition.wait( lock, [&] { return selection_barrier_open; } );
    }
};
```

Copy this mutex/CV pattern for every required interleaving. Use named barriers at the exact linearization points from `14-VALIDATION.md`; do not use sleeps to create races. For timeout behavior, inject a manual trigger/scheduler rather than waiting for production wall clock.

**Generation-race thread layout and assertions** (`test/src/multiaccount/multi_account_sync.cpp:641-701`):

```cpp
std::thread selector( [&] { /* SelectAccount repeatedly */ } );
std::thread reader( [&] { /* snapshot public account services */ } );
std::thread callback( [&] { /* inject stale callback */ } );
// ... join ...
for ( const auto &observed : observed_generations )
{
    if ( !observed.account || !observed.manager ) continue;
    ASSERT_EQ( observed.account_address, observed.manager_address );
    ASSERT_EQ( observed.generation, observed.catchup_generation );
}
ASSERT_EQ( stale_callback_side_effects.load(), 0U );
```

Strengthen the current blind spot: assert `bool(account) == bool(manager)` for every observation before checking addresses/generation. Add processing owner/lifecycle to the snapshot and assert that switching/failure never exposes a retired service. New tests should follow the exact names and behaviors in `14-VALIDATION.md`.

---

### `test/src/account/account_management_test.cpp` (public API integration test, request-response + event-driven wait)

**Primary analog:** existing selection test and fixture.

**Isolated node fixture with deterministic storage/config** (`test/src/account/account_management_test.cpp:121-152`):

```cpp
class AccountManagement : public ::testing::Test
{
public:
    static inline boost::filesystem::path path =
        boost::dll::program_location().parent_path() / "am_full_node";

    AccountManagement()
    {
        test::removeAllWithRetry( path.string() );
        boost::filesystem::create_directories( path );
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
            { return std::make_shared<MemorySecureStorage>( identifier ); } );
        // create node and confirm configured trust
        assert( node_->GetState() == GeniusNode::NodeState::READY );
    }
};
```

Keep the public contract tests in this fixture. If a lifecycle hook/scheduler is needed, expose it through the existing friend test access rather than adding production sleeps.

**Current asynchronous selection test shape** (`test/src/account/account_management_test.cpp:163-177`):

```cpp
auto old_account_address = node_->GetAddress();
auto new_account_address = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, path, true ).first->GetAddress();
ASSERT_TRUE( node_->SelectAccount( new_account_address ).has_value() );
test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                              std::chrono::milliseconds( 50000 ),
                              "node not synced" );
ASSERT_EQ( node_->GetAddress(), new_account_address );
```

Preserve the distinction between immediate acceptance and later readiness, but assert the returned acceptance generation and a generation-tagged ready/failure event. Add deterministic cases for `SWITCH_IN_PROGRESS`, overlapping selection rejection, unavailable address after failure, no automatic retry/republish, configured bootstrap identity separation, and explicit recovery.

## Shared Patterns

### Atomic Lifecycle Publication

**Source:** `src/account/GeniusNode.hpp:788-835`, `src/account/GeniusNode.cpp:630-644,3387-3407`

**Apply to:** all node lifecycle state, public account/manager/processing snapshots, switch acceptance, ready commit, and failure commit.

- One mutex owns lifecycle state, generation allocation, and ready/pending/retiring bundle moves.
- Public consumers copy a complete value snapshot while holding that mutex.
- Blocking drain/stop/join work happens after owners are copied or moved out and the mutex is released.
- Only a fully ready pending bundle is committed to `ready`; failure cleanup completes before unavailable state/event publication.

### Admission Closure and Terminal Drain

**Source:** partial analog in `src/account/TransactionManager.cpp:950-1085`; mutation boundaries at `src/account/TransactionManager.cpp:571-593,1120-1143`.

**Apply to:** transfer, mint, migration, hold/pay escrow, async pay, and any direct enqueue path.

- `TryAdmit` and `CloseAdmission` share one short manager mutex.
- The operation token is acquired before the first nonce/UTXO/state mutation.
- The token transfers into a transaction/operation ledger and completes exactly once at a canonical terminal transition.
- Drain-zero notification is dispatched after releasing admission/state locks.
- Drain timeout fails the switch but does not terminalize or cancel durable old work.

### Generation-Tagged Callback and Event Rejection

**Source:** `src/account/GeniusNode.cpp:890-928,1030-1046,3768-3808`.

**Apply to:** blockchain completion, posted manager initialization, manager readiness, processing readiness, ready/failure event emission, and old-operation terminal events.

- Capture weak node ownership plus generation and expected owner identity.
- Recheck generation and owner under the lifecycle snapshot before side effects.
- Terminal operation events carry the immutable generation stored at admission; never read the node's current generation at delivery time.
- Copy event payload/callback under lock and invoke after unlock.

### Typed Lifecycle Outcomes

**Source:** `src/account/GeniusNode.hpp:197-218`, `src/account/GeniusNode.cpp:146-184`, `src/account/TransactionManager.hpp:66-113`, `src/account/TransactionManager.cpp:37-45`.

**Apply to:** `SelectAccount`, active address and equivalent account-bound getters, processing status, manager mutations, and switch/operation event payloads.

Use explicit categories:

- Node: `SWITCH_IN_PROGRESS`, `ACCOUNT_UNAVAILABLE`.
- Manager: `MANAGER_RETIRED`.
- Preserve existing readiness/transaction errors where they remain semantically correct; do not encode lifecycle as empty `boost::system::error_code`, misspelled string sentinels, zero values, or `operation_aborted`.

### Lifecycle-Specific Status Snapshots

**Source:** coherent snapshot at `src/account/GeniusNode.cpp:3387-3407`; current status anti-pattern at `src/account/GeniusNode.hpp:531-540`.

**Apply to:** `GetProcessingStatus` and any status/diagnostic API that currently dereferences an independently owned service.

Snapshot `{lifecycle, generation, ready owners}` first. Map switching/unavailable without consulting a service. Only query the strong processing owner copied from a ready bundle, and include the ready generation in the returned status value.

### Deterministic Concurrency Verification

**Source:** `test/src/multiaccount/multi_account_sync.cpp:57-90,618-701`.

**Apply to:** all Wave 0 concurrency cases in `14-VALIDATION.md`.

Use friend-access hooks plus mutex/condition-variable barriers. Hooks should be default-empty, named for exact lifecycle boundaries, and invoked outside production locks except when a test explicitly observes a documented linearization point. Use a manually triggered timeout facility for D-08.

## No Exact Analog Found

These are required new contracts; the planner should combine the closest existing patterns above with `14-RESEARCH.md` rather than search outside the scoped architecture.

| Contract | Closest partial analog | Gap |
|---|---|---|
| Generation-bearing node ready/failure event payload and registration | `StateTransition` and generation/owner-guarded callbacks in `GeniusNode.cpp:630-644,890-928,1030-1046` | No public node-state callback/event registration API or generation-bearing lifecycle event exists in the scoped code. |
| Manager admission/retirement lifecycle and immutable retirement snapshot | Async wait ledger in `TransactionManager.hpp:349-367` and `TransactionManager.cpp:950-1085` | No `TryAdmit`, `CloseAdmission`, drain ledger, `MANAGER_RETIRED`, manager generation, or frozen retirement diagnostics exist. |
| Injectable lifecycle timeout scheduler | `PendingTransactionWait::timer` in `TransactionManager.hpp:349-367` | Existing timer is embedded in transaction observation and uses wall-clock expiry; Phase 14 needs a node switch/drain timeout seam that tests can trigger deterministically. |

## Anti-Patterns Identified in Current Code

- Do not clear switching at manager construction (`src/account/GeniusNode.cpp:1026-1028`); construction is not readiness.
- Do not publish `account_` before the manager/processing bundle is ready (`src/account/GeniusNode.cpp:2557-2567`).
- Do not use node state alone to validate blockchain completion (`src/account/GeniusNode.cpp:713-745`); bind generation and expected owner identity.
- Do not use `stopped_` as admission or call `Stop()` before drain (`src/account/TransactionManager.cpp:343-357,912-947`).
- Do not acquire a new permit at enqueue after nonce/UTXO mutation; carry one admission token from the first mutation boundary.
- Do not cancel accepted observers/operations on drain timeout (`src/account/TransactionManager.cpp:1088-1118` is shutdown behavior, not retirement behavior).
- Do not retain the partial-snapshot skip in `multi_account_sync.cpp:693-700`; assert owner presence equality.
- Do not introduce bridge, trust-refresh, or repository-wide storage-authority changes in this phase.

## Metadata

**Analog search scope:** `src/account`, `test/src/multiaccount`, `test/src/account`, with repository-wide pattern grep for weak ownership, generations, condition variables, and Outcome categories.

**Primary analog files read:** 6

**Pattern extraction date:** 2026-08-18

**Planner note:** The six files above form one coupled implementation. Plan Wave 0 deterministic hooks/tests before or alongside the lifecycle state changes, then implement manager admission/terminal drain before node switch orchestration, and finish with typed public lifecycle/status migration.
