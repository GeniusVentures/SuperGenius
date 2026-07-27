/**
 * @file consensus_vote_journal_test.cpp
 * @brief Deterministic persistent-store harness for Phase 10 vote-lock tests.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns
{
    /**
     * Test-only access surface for the private durable-vote hooks added by
     * later Phase 10 plans. Production fault-injection APIs are intentionally
     * not part of this harness.
     */
    class ConsensusVoteJournalTestAccess
    {
    public:
        static constexpr std::string_view Scope()
        {
            return "durable consensus vote journal";
        }
    };
} // namespace sgns

namespace
{
    constexpr auto kSystemClockNow =
        std::chrono::system_clock::time_point( std::chrono::milliseconds( 1'750'000'000'000LL ) );
    constexpr auto kSteadyClockNow =
        std::chrono::steady_clock::time_point( std::chrono::milliseconds( 42'000 ) );

    std::vector<uint8_t> DummySignature( std::vector<uint8_t> payload )
    {
        payload.push_back( 0x10 );
        return payload;
    }

    class ScopedReset
    {
    public:
        explicit ScopedReset( std::function<void()> reset ) : reset_( std::move( reset ) ) {}

        ScopedReset( const ScopedReset & )            = delete;
        ScopedReset &operator=( const ScopedReset & ) = delete;

        ~ScopedReset()
        {
            if ( reset_ )
            {
                reset_();
            }
        }

    private:
        std::function<void()> reset_;
    };

    struct VoteJournalCounters
    {
        std::atomic<uint64_t> signer{ 0 };
        std::atomic<uint64_t> raw_publish{ 0 };
        std::atomic<uint64_t> subscription{ 0 };
        std::atomic<uint64_t> timer{ 0 };
        std::atomic<uint64_t> certificate_filter{ 0 };

        void Reset()
        {
            signer.store( 0 );
            raw_publish.store( 0 );
            subscription.store( 0 );
            timer.store( 0 );
            certificate_filter.store( 0 );
        }
    };

    class ConsensusVoteJournalHarness : public test::CRDTFixture
    {
    public:
        ConsensusVoteJournalHarness() : CRDTFixture( "consensus_vote_journal_test" ) {}

    protected:
        std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry()
        {
            auto registry = sgns::ValidatorRegistry::New(
                db_,
                1,
                1,
                sgns::ValidatorRegistry::WeightConfig{},
                validator_id_,
                []( const std::string &, std::function<void( outcome::result<std::string> )> callback )
                { callback( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );
            if ( !registry )
            {
                return nullptr;
            }

            EXPECT_TRUE( registry->StoreGenesisRegistry( { validator_id_ }, DummySignature ).has_value() );
            ASSERT_WAIT_FOR_CONDITION(
                [&registry]()
                {
                    auto loaded = registry->LoadCurrentRegistry();
                    return loaded.has_value() && !registry->GetRegistryCid().empty();
                },
                std::chrono::milliseconds( 2000 ),
                "vote journal registry initialized",
                nullptr );
            return registry;
        }

        std::shared_ptr<sgns::ConsensusManager> MakeManager(
            const std::shared_ptr<sgns::ValidatorRegistry> &registry )
        {
            auto manager = sgns::ConsensusManager::New(
                registry,
                db_,
                pubs_,
                [this]( std::vector<uint8_t> payload ) -> outcome::result<std::vector<uint8_t>>
                {
                    ++counters_.signer;
                    return DummySignature( std::move( payload ) );
                },
                validator_id_ );
            EXPECT_TRUE( manager );
            if ( manager )
            {
                managers_.push_back( manager );
            }
            return manager;
        }

        void CloseManagers()
        {
            for ( auto &manager : managers_ )
            {
                if ( manager )
                {
                    manager->Close();
                }
            }
            managers_.clear();
        }

        std::shared_ptr<sgns::storage::rocksdb> CloseGlobalDBAndReopenStorage()
        {
            CloseManagers();
            db_->ShutdownNow();
            db_.reset();

            auto reopened = sgns::storage::rocksdb::create( db_path_ );
            EXPECT_TRUE( reopened.has_value() );
            return reopened.has_value() ? reopened.value() : nullptr;
        }

        VoteJournalCounters counters_;
        const std::chrono::system_clock::time_point system_clock_now_{ kSystemClockNow };
        const std::chrono::steady_clock::time_point steady_clock_now_{ kSteadyClockNow };

    private:
        const std::string validator_id_{ "validator-vote-journal" };
        std::vector<std::shared_ptr<sgns::ConsensusManager>> managers_;
    };
} // namespace

TEST_F( ConsensusVoteJournalHarness, PersistentDatabaseReopensCleanly )
{
    const ScopedReset reset_counters( [this]() { counters_.Reset(); } );
    ASSERT_LT( steady_clock_now_.time_since_epoch(), system_clock_now_.time_since_epoch() );

    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );

    sgns::base::Buffer key;
    key.put( "/consensus/local/v2/harness/reopen" );
    sgns::base::Buffer value;
    value.put( "same-path-marker" );
    auto first_store = db_->GetDataStore();
    ASSERT_TRUE( first_store );
    ASSERT_TRUE( first_store->put( key, value ).has_value() );
    first_store.reset();
    manager.reset();
    registry.reset();

    auto reopened = CloseGlobalDBAndReopenStorage();
    ASSERT_TRUE( reopened );
    auto stored = reopened->get( key );
    ASSERT_TRUE( stored.has_value() );
    EXPECT_EQ( stored.value().toString(), "same-path-marker" );
}
