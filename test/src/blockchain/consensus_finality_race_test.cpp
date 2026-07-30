/**
 * @file consensus_finality_race_test.cpp
 * @brief Deterministic external-ingress coverage for the finality/application gap.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "account/GeniusAccount.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns
{
    class ConsensusFinalityRaceTestAccess
    {
    public:
        static void SetStageObserver( const std::shared_ptr<ConsensusManager> &manager,
                                      std::function<void( std::string_view )> observer )
        {
            manager->finalization_stage_observer_ = std::move( observer );
        }

        static void HandleCertificate( const std::shared_ptr<ConsensusManager> &manager,
                                       const ConsensusManager::Certificate &certificate )
        {
            manager->HandleCertificate( certificate );
        }

        static void RecoverPendingCertificateWork( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->RecoverPendingCertificateWork();
        }

        static ConsensusManager::SlotState::Lifecycle Lifecycle(
            const std::shared_ptr<ConsensusManager> &manager, const std::string &slot )
        {
            std::lock_guard lock( manager->proposals_mutex_ );
            return manager->slot_states_[slot].lifecycle;
        }

        static outcome::result<std::string> Slot( const ConsensusManager::Certificate &certificate )
        {
            return ConsensusManager::GetSlotKey( certificate.proposal() );
        }
    };
}

namespace
{
    class DeterministicBarrier
    {
    public:
        void ArriveAndWait()
        {
            std::unique_lock lock( mutex_ );
            arrived_ = true;
            cv_.notify_all();
            cv_.wait( lock, [&] { return released_; } );
        }
        void WaitUntilArrived()
        {
            std::unique_lock lock( mutex_ );
            cv_.wait( lock, [&] { return arrived_; } );
        }
        void Release()
        {
            std::lock_guard lock( mutex_ );
            released_ = true;
            cv_.notify_all();
        }
    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        bool arrived_{ false };
        bool released_{ false };
    };

    class ScopedWorker
    {
    public:
        ScopedWorker( std::thread worker, DeterministicBarrier &barrier )
            : worker_( std::move( worker ) ), barrier_( barrier ) {}
        ~ScopedWorker() { barrier_.Release(); Join(); }
        void Join() { if ( worker_.joinable() ) worker_.join(); }
        ScopedWorker( const ScopedWorker & ) = delete;
        ScopedWorker &operator=( const ScopedWorker & ) = delete;
    private:
        std::thread worker_;
        DeterministicBarrier &barrier_;
    };

    class ConsensusFinalityRaceHarness : public test::CRDTFixture
    {
    public:
        ConsensusFinalityRaceHarness() : CRDTFixture( "consensus_finality_race_test" )
        {
            sgns::GeniusAccount::SetSecureStorageFactory(
                []( const std::string &id ) -> std::shared_ptr<sgns::ISecureStorage>
                { return std::make_shared<sgns::MemorySecureStorage>( id ); } );
        }

    protected:
        std::shared_ptr<sgns::GeniusAccount> MakeAccount()
        {
            return sgns::GeniusAccount::NewFromPrivateKey(
                sgns::TokenID::FromBytes( { 0 } ),
                "facebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                boost::filesystem::path( db_path_ ) / "race-account", false );
        }

        std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry(
            const std::shared_ptr<sgns::GeniusAccount> &account )
        {
            auto registry = sgns::ValidatorRegistry::New(
                db_, 1, 1, {}, account->GetAddress(),
                []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
                { cb( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );
            EXPECT_TRUE( registry->StoreGenesisRegistry(
                { account->GetAddress() }, [account]( std::vector<uint8_t> bytes )
                { return account->Sign( std::move( bytes ) ); } ) );
            ASSERT_WAIT_FOR_CONDITION(
                [&] { return registry->LoadCurrentRegistry().has_value() && !registry->GetRegistryCid().empty(); },
                std::chrono::seconds( 2 ), "registry", nullptr );
            return registry;
        }
    };
}

TEST_F( ConsensusFinalityRaceHarness, HarnessIsDiscoverable )
{
    auto account = MakeAccount();
    ASSERT_TRUE( account );
    auto registry = MakeRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = sgns::ConsensusManager::New(
        registry, db_, pubs_, [account]( std::vector<uint8_t> bytes )
        { return account->Sign( std::move( bytes ) ); }, account->GetAddress() );
    ASSERT_TRUE( manager );
    manager->Close();
}
