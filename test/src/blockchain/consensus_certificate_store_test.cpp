#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "account/GeniusAccount.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "crdt/crdt_data_filter.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns
{
    class ConsensusManagerTestAccess
    {
    public:
        static outcome::result<std::string> Slot( const ConsensusManager::Certificate &certificate )
        {
            return ConsensusManager::GetSlotKey( certificate.proposal() );
        }

        static outcome::result<std::string> Winner( const ConsensusManager::Certificate &certificate )
        {
            return ConsensusManager::GetSubjectHash( certificate.proposal().subject() );
        }

        static bool FilterDelta( const std::shared_ptr<ConsensusManager> &manager, const crdt::pb::Delta &delta )
        {
            return manager && manager->FilterCertificateDelta( delta );
        }
    };
} // namespace sgns

namespace sgns::test
{
    namespace
    {
        constexpr std::string_view kV2Pattern = "^/?cert/v2/(slot|tx)/[0-9a-f]{64}$";
        constexpr std::string_view kSlot =
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        constexpr std::string_view kTx =
            "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

        crdt::pb::Delta MakePairDelta()
        {
            crdt::pb::Delta delta;
            auto           *slot = delta.add_elements();
            slot->set_key( "/cert/v2/slot/" + std::string( kSlot ) );
            slot->set_value( "certificate" );
            auto *index = delta.add_elements();
            index->set_key( "/cert/v2/tx/" + std::string( kTx ) );
            index->set_value( std::string( kSlot ) );
            return delta;
        }

        bool HasCompletePair( const crdt::pb::Delta &delta )
        {
            bool has_slot  = false;
            bool has_index = false;
            for ( const auto &element : delta.elements() )
            {
                has_slot |= element.key() == "/cert/v2/slot/" + std::string( kSlot ) &&
                            element.value() == "certificate";
                has_index |= element.key() == "/cert/v2/tx/" + std::string( kTx ) &&
                             element.value() == kSlot;
            }
            return has_slot && has_index;
        }

        constexpr const char *kPrivateKey =
            "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
        constexpr const char *kPrivateKey2 =
            "deedbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

        std::shared_ptr<GeniusAccount> MakeAccount( const std::string &path, const char *private_key = kPrivateKey )
        {
            return GeniusAccount::NewFromPrivateKey(
                TokenID::FromBytes( { 0x00 } ),
                private_key,
                boost::filesystem::path( path ) / std::string( private_key, 4 ),
                false );
        }

        std::shared_ptr<ValidatorRegistry> MakeRegistry( const std::shared_ptr<crdt::GlobalDB> &db,
                                                         const std::shared_ptr<GeniusAccount>  &account )
        {
            if ( !db || !account )
            {
                return nullptr;
            }
            auto registry = ValidatorRegistry::New(
                db,
                1,
                1,
                ValidatorRegistry::WeightConfig{},
                account->GetAddress(),
                []( const std::string &, std::function<void( outcome::result<std::string> )> callback )
                { callback( outcome::failure( std::errc::not_supported ) ); } );
            if ( !registry )
            {
                return nullptr;
            }
            auto stored = registry->StoreGenesisRegistry(
                { account->GetAddress() },
                [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
            if ( stored.has_error() )
            {
                return nullptr;
            }
            for ( int attempt = 0; attempt < 100; ++attempt )
            {
                auto loaded = registry->LoadCurrentRegistry();
                if ( loaded.has_value() && !registry->GetRegistryCid().empty() )
                {
                    return registry;
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            }
            return nullptr;
        }

        std::shared_ptr<ConsensusManager> MakeManager( const std::shared_ptr<ValidatorRegistry> &registry,
                                                       const std::shared_ptr<crdt::GlobalDB>    &db,
                                                       const std::shared_ptr<ipfs_pubsub::GossipPubSub> &pubsub,
                                                       const std::shared_ptr<GeniusAccount> &account )
        {
            return ConsensusManager::New(
                registry,
                db,
                pubsub,
                [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
                account->GetAddress() );
        }

        UTXOTransitionCommitment MakeCommitment()
        {
            UTXOTransitionCommitment commitment;
            auto                    *consumed = commitment.add_consumed_outpoints();
            consumed->set_tx_id_hash( std::string( 32, '\x01' ) );
            consumed->set_output_index( 0 );
            commitment.set_consumed_outpoints_root( std::string( 32, '\x02' ) );
            commitment.set_produced_outputs_root( std::string( 32, '\x03' ) );
            return commitment;
        }

        outcome::result<ConsensusManager::Certificate> MakeCertificate(
            const std::shared_ptr<ConsensusManager> &manager,
            const std::shared_ptr<ValidatorRegistry> &registry,
            const std::shared_ptr<GeniusAccount> &voter,
            const std::shared_ptr<GeniusAccount> &proposer,
            const ConsensusManager::Subject      *existing_subject = nullptr )
        {
            ConsensusManager::Subject subject;
            if ( existing_subject )
            {
                subject = *existing_subject;
            }
            else
            {
                BOOST_OUTCOME_TRY(
                    auto created,
                    ConsensusManager::CreateNonceSubject(
                        voter->GetAddress(),
                        7,
                        "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
                        EmbeddedTransaction{},
                        MakeCommitment(),
                        UTXOWitness{} ) );
                subject = std::move( created );
            }

            BOOST_OUTCOME_TRY(
                auto proposal,
                ConsensusManager::CreateProposal(
                    subject,
                    proposer->GetAddress(),
                    registry->GetRegistryCid(),
                    registry->GetRegistryEpoch(),
                    [proposer]( std::vector<uint8_t> payload ) { return proposer->Sign( std::move( payload ) ); } ) );
            BOOST_OUTCOME_TRY(
                auto vote,
                manager->CreateVote(
                    proposal.proposal_id(),
                    voter->GetAddress(),
                    true,
                    [voter]( std::vector<uint8_t> payload ) { return voter->Sign( std::move( payload ) ); } ) );
            return manager->CreateCertificate( proposal, { vote } );
        }

        crdt::pb::Delta MakeCertificatePairDelta( const ConsensusManager::Certificate &certificate )
        {
            auto slot   = ConsensusManagerTestAccess::Slot( certificate ).value();
            auto winner = ConsensusManagerTestAccess::Winner( certificate ).value();
            std::string serialized;
            certificate.SerializeToString( &serialized );

            crdt::pb::Delta delta;
            auto           *slot_element = delta.add_elements();
            slot_element->set_key( "/cert/v2/slot/" + slot );
            slot_element->set_value( serialized );
            auto *index_element = delta.add_elements();
            index_element->set_key( "/cert/v2/tx/" + winner );
            index_element->set_value( slot );
            return delta;
        }
    } // namespace

    class ConsensusCertificateStoreTest : public ::test::CRDTFixture
    {
    public:
        ConsensusCertificateStoreTest() : CRDTFixture( "ConsensusCertificateStoreTest" )
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
        }
    };

    TEST_F( ConsensusCertificateStoreTest, DeltaFilterAcceptsValidPair )
    {
        crdt::CRDTDataFilter filter( db_->GetWorkJournal() );
        ASSERT_TRUE( filter.RegisterDeltaFilter( std::string( kV2Pattern ), HasCompletePair ) );

        auto delta = MakePairDelta();
        filter.FilterElementsOnDelta( delta );

        ASSERT_EQ( delta.elements_size(), 2 );
        EXPECT_TRUE( HasCompletePair( delta ) );
    }

    TEST_F( ConsensusCertificateStoreTest, DeltaFilterRejectsPartialPairAtomically )
    {
        crdt::CRDTDataFilter filter( db_->GetWorkJournal() );
        ASSERT_TRUE( filter.RegisterDeltaFilter( std::string( kV2Pattern ), HasCompletePair ) );

        auto delta = MakePairDelta();
        delta.mutable_elements()->DeleteSubrange( 1, 1 );
        auto *unrelated = delta.add_elements();
        unrelated->set_key( "/unrelated/value" );
        unrelated->set_value( "preserved" );

        filter.FilterElementsOnDelta( delta );

        ASSERT_EQ( delta.elements_size(), 1 );
        EXPECT_EQ( delta.elements( 0 ).key(), "/unrelated/value" );
    }

    TEST_F( ConsensusCertificateStoreTest, PairFilterSeesCompleteDeltaBeforeElementFilters )
    {
        crdt::CRDTDataFilter filter( db_->GetWorkJournal() );
        bool                 saw_complete_pair = false;
        ASSERT_TRUE( filter.RegisterDeltaFilter(
            std::string( kV2Pattern ),
            [&saw_complete_pair]( const crdt::pb::Delta &delta )
            {
                saw_complete_pair = HasCompletePair( delta );
                return saw_complete_pair;
            } ) );
        ASSERT_TRUE( filter.RegisterElementFilter(
            "^/?cert/v2/tx/[0-9a-f]{64}$",
            []( const crdt::pb::Element & )
            { return std::optional<std::vector<crdt::pb::Element>>( std::vector<crdt::pb::Element>{} ); } ) );

        auto delta = MakePairDelta();
        filter.FilterElementsOnDelta( delta );

        EXPECT_TRUE( saw_complete_pair );
        ASSERT_EQ( delta.elements_size(), 1 );
        EXPECT_EQ( delta.elements( 0 ).key(), "/cert/v2/slot/" + std::string( kSlot ) );
    }

    TEST_F( ConsensusCertificateStoreTest, SubmitStoresAuthoritativeSlotAndWinningIndexAtomically )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        auto certificate = MakeCertificate( manager, registry, account, account );
        ASSERT_TRUE( certificate.has_value() );

        ASSERT_TRUE( manager->SubmitCertificate( certificate.value() ).has_value() );
        auto slot   = ConsensusManagerTestAccess::Slot( certificate.value() ).value();
        auto winner = ConsensusManagerTestAccess::Winner( certificate.value() ).value();
        auto stored = db_->Get( { "/cert/v2/slot/" + slot } );
        auto index  = db_->Get( { "/cert/v2/tx/" + winner } );

        ASSERT_TRUE( stored.has_value() );
        ASSERT_TRUE( index.has_value() );
        EXPECT_EQ( std::string( index.value().toString() ), slot );
        ConsensusManager::Certificate parsed;
        EXPECT_TRUE( parsed.ParseFromArray( stored.value().data(), stored.value().size() ) );
        EXPECT_EQ( parsed.SerializeAsString(), certificate.value().SerializeAsString() );
        auto fetched = manager->GetCertificateBySubjectHash( winner );
        ASSERT_TRUE( fetched.has_value() );
        EXPECT_EQ( fetched.value().SerializeAsString(), certificate.value().SerializeAsString() );
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, ReplayIsIdempotentWithoutDuplicateCallback )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        auto certificate = MakeCertificate( manager, registry, account, account );
        ASSERT_TRUE( certificate.has_value() );

        std::atomic<int> callbacks{ 0 };
        ASSERT_TRUE( manager->RegisterCertificateHandler(
            NONCE_SUBJECT_TYPE,
            [&callbacks]( const std::string &, const ConsensusManager::Certificate & )
            {
                ++callbacks;
                return outcome::success( ConsensusManager::Check::Approve );
            } ) );
        ASSERT_TRUE( manager->SubmitCertificate( certificate.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION( [&callbacks]() { return callbacks.load() == 1; },
                                   std::chrono::milliseconds( 2000 ),
                                   "first certificate callback",
                                   nullptr );

        EXPECT_TRUE( manager->SubmitCertificate( certificate.value() ).has_value() );
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        EXPECT_EQ( callbacks.load(), 1 );
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, ConflictRejectsDifferentCertificateForOccupiedSlot )
    {
        auto voter     = MakeAccount( getPathString() );
        auto proposer2 = MakeAccount( getPathString(), kPrivateKey2 );
        auto registry  = MakeRegistry( db_, voter );
        auto manager   = MakeManager( registry, db_, pubs_, voter );
        ASSERT_TRUE( voter && proposer2 && registry && manager );
        auto first = MakeCertificate( manager, registry, voter, voter );
        ASSERT_TRUE( first.has_value() );
        auto second = MakeCertificate( manager, registry, voter, proposer2, &first.value().proposal().subject() );
        ASSERT_TRUE( second.has_value() );
        ASSERT_NE( first.value().proposal_id(), second.value().proposal_id() );

        ASSERT_TRUE( manager->SubmitCertificate( first.value() ).has_value() );
        auto conflict = manager->SubmitCertificate( second.value() );
        ASSERT_TRUE( conflict.has_error() );
        EXPECT_EQ( conflict.error(), make_error_code( ConsensusManager::CertificateStoreError::Conflict ) );

        auto slot   = ConsensusManagerTestAccess::Slot( first.value() ).value();
        auto stored = db_->Get( { "/cert/v2/slot/" + slot } );
        ASSERT_TRUE( stored.has_value() );
        ConsensusManager::Certificate parsed;
        ASSERT_TRUE( parsed.ParseFromArray( stored.value().data(), stored.value().size() ) );
        EXPECT_EQ( parsed.proposal_id(), first.value().proposal_id() );
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, DeltaRejectsMalformedPartialAndMismatchedCertificatePairs )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        auto certificate = MakeCertificate( manager, registry, account, account );
        ASSERT_TRUE( certificate.has_value() );

        auto valid = MakeCertificatePairDelta( certificate.value() );
        EXPECT_TRUE( ConsensusManagerTestAccess::FilterDelta( manager, valid ) );

        auto partial = valid;
        partial.mutable_elements()->DeleteSubrange( 1, 1 );
        EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta( manager, partial ) );

        auto mismatched = valid;
        mismatched.mutable_elements( 1 )->set_value( std::string( 64, '0' ) );
        EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta( manager, mismatched ) );

        auto malformed = valid;
        malformed.mutable_elements( 0 )->set_key( "/cert/v2/slot/not-a-slot" );
        EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta( manager, malformed ) );

        auto tampered = certificate.value();
        tampered.mutable_votes( 0 )->set_signature( "invalid" );
        auto invalid_signature = MakeCertificatePairDelta( tampered );
        EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta( manager, invalid_signature ) );
        manager->Close();
    }
} // namespace sgns::test
