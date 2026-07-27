#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>

#include <google/protobuf/unknown_field_set.h>

#include "account/GeniusAccount.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "crdt/crdt_data_filter.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "storage/database_error.hpp"
#include "testutil/outcome.hpp"
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

        static crdt::DeltaFilterResult FilterDeltaResult(
            const std::shared_ptr<ConsensusManager> &manager,
            const crdt::pb::Delta                   &delta )
        {
            return manager ? manager->FilterCertificateDelta( delta )
                           : crdt::DeltaFilterResult::Reject();
        }

        static void SetCertificateReader(
            const std::shared_ptr<ConsensusManager> &manager,
            std::function<outcome::result<crdt::GlobalDB::Buffer>( const crdt::HierarchicalKey & )> reader )
        {
            manager->certificate_record_reader_ = std::move( reader );
        }

        static void ResetCertificateReader( const std::shared_ptr<ConsensusManager> &manager )
        {
            auto db = manager->db_;
            manager->certificate_record_reader_ =
                [db = std::move( db )]( const crdt::HierarchicalKey &key ) { return db->Get( key ); };
        }

        static void SetCertificatePublishObserver( const std::shared_ptr<ConsensusManager> &manager,
                                                   std::function<void()>                    observer )
        {
            manager->certificate_publish_observer_ = std::move( observer );
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
        constexpr const char *kPrivateKey3 =
            "feedbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

        std::shared_ptr<GeniusAccount> MakeAccount( const std::string &path, const char *private_key = kPrivateKey )
        {
            return GeniusAccount::NewFromPrivateKey(
                TokenID::FromBytes( { 0x00 } ),
                private_key,
                boost::filesystem::path( path ) / std::string( private_key, 4 ),
                false );
        }

        std::shared_ptr<ValidatorRegistry> MakeRegistry(
            const std::shared_ptr<crdt::GlobalDB>              &db,
            const std::vector<std::shared_ptr<GeniusAccount>> &accounts )
        {
            if ( !db || accounts.empty() || !accounts.front() )
            {
                return nullptr;
            }
            std::vector<std::string> validator_ids;
            validator_ids.reserve( accounts.size() );
            for ( const auto &account : accounts )
            {
                if ( !account )
                {
                    return nullptr;
                }
                validator_ids.push_back( account->GetAddress() );
            }
            auto registry = ValidatorRegistry::New(
                db,
                1,
                1,
                ValidatorRegistry::WeightConfig{},
                accounts.front()->GetAddress(),
                []( const std::string &, std::function<void( outcome::result<std::string> )> callback )
                { callback( outcome::failure( std::errc::not_supported ) ); } );
            if ( !registry )
            {
                return nullptr;
            }
            auto stored = registry->StoreGenesisRegistry(
                validator_ids,
                [account = accounts.front()]( std::vector<uint8_t> payload )
                { return account->Sign( std::move( payload ) ); } );
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

        std::shared_ptr<ValidatorRegistry> MakeRegistry( const std::shared_ptr<crdt::GlobalDB> &db,
                                                         const std::shared_ptr<GeniusAccount>  &account )
        {
            return MakeRegistry( db, std::vector<std::shared_ptr<GeniusAccount>>{ account } );
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

        outcome::result<ConsensusManager::Certificate> MakeTwoVoteCertificate(
            const std::shared_ptr<ConsensusManager> &manager,
            const std::shared_ptr<ValidatorRegistry> &registry,
            const std::shared_ptr<GeniusAccount> &first,
            const std::shared_ptr<GeniusAccount> &second,
            bool                                  reverse_input = false )
        {
            BOOST_OUTCOME_TRY(
                auto subject,
                ConsensusManager::CreateNonceSubject(
                    first->GetAddress(),
                    7,
                    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
                    EmbeddedTransaction{},
                    MakeCommitment(),
                    UTXOWitness{} ) );
            BOOST_OUTCOME_TRY(
                auto proposal,
                ConsensusManager::CreateProposal(
                    subject,
                    first->GetAddress(),
                    registry->GetRegistryCid(),
                    registry->GetRegistryEpoch(),
                    [first]( std::vector<uint8_t> payload ) { return first->Sign( std::move( payload ) ); } ) );
            BOOST_OUTCOME_TRY(
                auto first_vote,
                manager->CreateVote(
                    proposal.proposal_id(),
                    first->GetAddress(),
                    true,
                    [first]( std::vector<uint8_t> payload ) { return first->Sign( std::move( payload ) ); } ) );
            BOOST_OUTCOME_TRY(
                auto second_vote,
                manager->CreateVote(
                    proposal.proposal_id(),
                    second->GetAddress(),
                    true,
                    [second]( std::vector<uint8_t> payload ) { return second->Sign( std::move( payload ) ); } ) );
            std::vector<ConsensusManager::Vote> votes{ first_vote, second_vote };
            if ( reverse_input )
            {
                std::reverse( votes.begin(), votes.end() );
            }
            return manager->CreateCertificate( proposal, votes );
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

        outcome::result<CID> PutRawCertificateState( const std::shared_ptr<crdt::GlobalDB> &db,
                                                      const std::string                     &key,
                                                      std::string_view                       value )
        {
            crdt::GlobalDB::Buffer buffer;
            buffer.put( value );
            return db->Put( { key }, buffer, {} );
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

    TEST_F( ConsensusCertificateStoreTest, SubmitCertificatePreflightReadErrorsFailClosed )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        ASSERT_OUTCOME_SUCCESS( certificate, MakeCertificate( manager, registry, account, account ) );

        const auto slot   = ConsensusManagerTestAccess::Slot( certificate ).value();
        const auto winner = ConsensusManagerTestAccess::Winner( certificate ).value();
        const auto slot_key  = "/cert/v2/slot/" + slot;
        const auto index_key = "/cert/v2/tx/" + winner;

        std::atomic<int> callbacks{ 0 };
        std::atomic<int> publishes{ 0 };
        ASSERT_TRUE( manager->RegisterCertificateHandler(
            NONCE_SUBJECT_TYPE,
            [&callbacks]( const std::string &, const ConsensusManager::Certificate & )
            {
                ++callbacks;
                return outcome::success( ConsensusManager::Check::Approve );
            } ) );
        ConsensusManagerTestAccess::SetCertificatePublishObserver( manager, [&publishes]() { ++publishes; } );

        struct ReadRow
        {
            const char            *name;
            storage::DatabaseError slot_error;
            storage::DatabaseError index_error;
            std::optional<ConsensusManager::CertificateStoreError> expected_error;
        };
        const std::array<ReadRow, 7> rows{
            ReadRow{ "both not found",
                     storage::DatabaseError::NOT_FOUND,
                     storage::DatabaseError::NOT_FOUND,
                     std::nullopt },
            ReadRow{ "slot corruption",
                     storage::DatabaseError::CORRUPTION,
                     storage::DatabaseError::NOT_FOUND,
                     ConsensusManager::CertificateStoreError::IntegrityError },
            ReadRow{ "index corruption",
                     storage::DatabaseError::NOT_FOUND,
                     storage::DatabaseError::CORRUPTION,
                     ConsensusManager::CertificateStoreError::IntegrityError },
            ReadRow{ "both corruption",
                     storage::DatabaseError::CORRUPTION,
                     storage::DatabaseError::CORRUPTION,
                     ConsensusManager::CertificateStoreError::IntegrityError },
            ReadRow{ "slot I/O",
                     storage::DatabaseError::IO_ERROR,
                     storage::DatabaseError::NOT_FOUND,
                     ConsensusManager::CertificateStoreError::StorageError },
            ReadRow{ "index I/O",
                     storage::DatabaseError::NOT_FOUND,
                     storage::DatabaseError::IO_ERROR,
                     ConsensusManager::CertificateStoreError::StorageError },
            ReadRow{ "both I/O",
                     storage::DatabaseError::IO_ERROR,
                     storage::DatabaseError::IO_ERROR,
                     ConsensusManager::CertificateStoreError::StorageError },
        };

        for ( const auto &row : rows )
        {
            SCOPED_TRACE( row.name );
            int slot_reads  = 0;
            int index_reads = 0;
            ConsensusManagerTestAccess::SetCertificateReader(
                manager,
                [&, slot_error = row.slot_error, index_error = row.index_error](
                    const crdt::HierarchicalKey &key ) -> outcome::result<crdt::GlobalDB::Buffer>
                {
                    if ( key.GetKey() == slot_key )
                    {
                        ++slot_reads;
                        return outcome::failure( slot_error );
                    }
                    if ( key.GetKey() == index_key )
                    {
                        ++index_reads;
                        return outcome::failure( index_error );
                    }
                    ADD_FAILURE() << "unexpected certificate preflight key: " << key.GetKey();
                    return db_->Get( key );
                } );

            const auto before_slot     = db_->Get( { slot_key } );
            const auto before_index    = db_->Get( { index_key } );
            const auto before_callback = callbacks.load();
            const auto before_publish  = publishes.load();
            auto       result          = manager->SubmitCertificate( certificate );
            ConsensusManagerTestAccess::ResetCertificateReader( manager );

            if ( row.expected_error )
            {
                EXPECT_EQ( slot_reads, 1 );
                EXPECT_EQ( index_reads, 1 );
                ASSERT_TRUE( result.has_error() );
                EXPECT_EQ( result.error(), make_error_code( *row.expected_error ) );
                auto after_slot  = db_->Get( { slot_key } );
                auto after_index = db_->Get( { index_key } );
                ASSERT_EQ( after_slot.has_value(), before_slot.has_value() );
                ASSERT_EQ( after_index.has_value(), before_index.has_value() );
                if ( before_slot )
                {
                    EXPECT_EQ( after_slot.value().toString(), before_slot.value().toString() );
                }
                if ( before_index )
                {
                    EXPECT_EQ( after_index.value().toString(), before_index.value().toString() );
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
                EXPECT_EQ( callbacks.load(), before_callback );
                EXPECT_EQ( publishes.load(), before_publish );
            }
            else
            {
                EXPECT_GE( slot_reads, 1 );
                EXPECT_GE( index_reads, 1 );
                ASSERT_TRUE( result.has_value() );
                ASSERT_WAIT_FOR_CONDITION( [&callbacks]() { return callbacks.load() >= 1; },
                                           std::chrono::milliseconds( 2000 ),
                                           "certificate callback after the NOT_FOUND control",
                                           nullptr );
                EXPECT_EQ( publishes.load(), 1 );
                EXPECT_TRUE( db_->Get( { slot_key } ).has_value() );
                EXPECT_TRUE( db_->Get( { index_key } ).has_value() );
            }
        }

        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, FilterCertificateDeltaPreflightReadErrorsFailClosed )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        ASSERT_OUTCOME_SUCCESS( certificate, MakeCertificate( manager, registry, account, account ) );

        const auto delta     = MakeCertificatePairDelta( certificate );
        const auto slot      = ConsensusManagerTestAccess::Slot( certificate ).value();
        const auto winner    = ConsensusManagerTestAccess::Winner( certificate ).value();
        const auto slot_key  = "/cert/v2/slot/" + slot;
        const auto index_key = "/cert/v2/tx/" + winner;

        struct ReadRow
        {
            const char            *name;
            storage::DatabaseError slot_error;
            storage::DatabaseError index_error;
            crdt::DeltaFilterDecision expected;
            bool                       deliver;
        };
        const std::array<ReadRow, 7> rows{
            ReadRow{ "both not found",
                     storage::DatabaseError::NOT_FOUND,
                     storage::DatabaseError::NOT_FOUND,
                     crdt::DeltaFilterDecision::Approve,
                     false },
            ReadRow{ "slot corruption",
                     storage::DatabaseError::CORRUPTION,
                     storage::DatabaseError::NOT_FOUND,
                     crdt::DeltaFilterDecision::Reject,
                     true },
            ReadRow{ "index corruption",
                     storage::DatabaseError::NOT_FOUND,
                     storage::DatabaseError::CORRUPTION,
                     crdt::DeltaFilterDecision::Reject,
                     false },
            ReadRow{ "both corruption",
                     storage::DatabaseError::CORRUPTION,
                     storage::DatabaseError::CORRUPTION,
                     crdt::DeltaFilterDecision::Reject,
                     false },
            ReadRow{ "slot I/O",
                     storage::DatabaseError::IO_ERROR,
                     storage::DatabaseError::NOT_FOUND,
                     crdt::DeltaFilterDecision::Reject,
                     false },
            ReadRow{ "index I/O",
                     storage::DatabaseError::NOT_FOUND,
                     storage::DatabaseError::IO_ERROR,
                     crdt::DeltaFilterDecision::Reject,
                     true },
            ReadRow{ "both I/O",
                     storage::DatabaseError::IO_ERROR,
                     storage::DatabaseError::IO_ERROR,
                     crdt::DeltaFilterDecision::Reject,
                     false },
        };

        for ( const auto &row : rows )
        {
            SCOPED_TRACE( row.name );
            int slot_reads  = 0;
            int index_reads = 0;
            ConsensusManagerTestAccess::SetCertificateReader(
                manager,
                [&, slot_error = row.slot_error, index_error = row.index_error](
                    const crdt::HierarchicalKey &key ) -> outcome::result<crdt::GlobalDB::Buffer>
                {
                    if ( key.GetKey() == slot_key )
                    {
                        ++slot_reads;
                        return outcome::failure( slot_error );
                    }
                    if ( key.GetKey() == index_key )
                    {
                        ++index_reads;
                        return outcome::failure( index_error );
                    }
                    ADD_FAILURE() << "unexpected certificate preflight key: " << key.GetKey();
                    return db_->Get( key );
                } );

            const auto result = ConsensusManagerTestAccess::FilterDeltaResult( manager, delta );
            EXPECT_EQ( result.decision, row.expected );
            EXPECT_NE( result.decision, crdt::DeltaFilterDecision::RetryDependency );
            EXPECT_EQ( slot_reads, 1 );
            EXPECT_EQ( index_reads, 1 );

            if ( row.deliver )
            {
                crdt::CRDTDataFilter receiver_filter( db_->GetWorkJournal() );
                ASSERT_TRUE( receiver_filter.RegisterDeltaFilter(
                    "^/?cert/.*$",
                    [manager]( const crdt::pb::Delta &incoming )
                    { return ConsensusManagerTestAccess::FilterDeltaResult( manager, incoming ); } ) );
                auto filtered = receiver_filter.FilterDelta( delta );
                EXPECT_EQ( filtered.decision, crdt::DeltaFilterDecision::Reject );
                EXPECT_EQ( filtered.delta.elements_size(), 0 );
                ASSERT_TRUE(
                    db_->GetCRDTDataStore()
                        ->Publish( std::make_shared<crdt::pb::Delta>( filtered.delta ), {} )
                        .has_value() );
                EXPECT_TRUE( db_->Get( { slot_key } ).has_error() );
                EXPECT_TRUE( db_->Get( { index_key } ).has_error() );
            }

            ConsensusManagerTestAccess::ResetCertificateReader( manager );
        }

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

    TEST_F( ConsensusCertificateStoreTest, DeltaRejectsEverySignedWinningProposalSurfaceMutation )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        auto certificate = MakeCertificate( manager, registry, account, account );
        ASSERT_TRUE( certificate.has_value() );

        const auto valid_delta = MakeCertificatePairDelta( certificate.value() );
        const auto slot_key    = valid_delta.elements( 0 ).key();
        const auto index_key   = valid_delta.elements( 1 ).key();
        auto expect_rejected   = [&]( std::string_view name, auto mutate )
        {
            SCOPED_TRACE( std::string( name ) );
            auto tampered = certificate.value();
            mutate( tampered );
            std::string bytes;
            ASSERT_TRUE( tampered.SerializeToString( &bytes ) );
            auto delta = valid_delta;
            delta.mutable_elements( 0 )->set_value( bytes );
            EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta( manager, delta ) );
            EXPECT_TRUE( db_->Get( { slot_key } ).has_error() );
            EXPECT_TRUE( db_->Get( { index_key } ).has_error() );
        };

        expect_rejected( "subject account identity",
                         []( auto &cert ) { cert.mutable_proposal()->mutable_subject()->set_account_id( "tampered" ); } );
        expect_rejected( "subject type",
                         []( auto &cert )
                         {
                             cert.mutable_proposal()
                                 ->mutable_subject()
                                 ->mutable_subject_type_hash()
                                 ->set_hash( std::string( 32, '\x44' ) );
                         } );
        expect_rejected( "proposer identity",
                         []( auto &cert ) { cert.mutable_proposal()->set_proposer_id( "tampered" ); } );
        expect_rejected( "proposal id",
                         []( auto &cert ) { cert.mutable_proposal()->set_proposal_id( std::string( 64, '0' ) ); } );
        expect_rejected( "nonce",
                         []( auto &cert )
                         {
                             auto payload = ConsensusManager::DecodeNonceSubject( cert.proposal().subject() ).value();
                             payload.set_nonce( payload.nonce() + 1 );
                             payload.SerializeToString( cert.mutable_proposal()->mutable_subject()->mutable_payload() );
                         } );
        expect_rejected( "transaction hash",
                         []( auto &cert )
                         {
                             auto payload = ConsensusManager::DecodeNonceSubject( cert.proposal().subject() ).value();
                             payload.set_tx_hash( std::string( 64, 'f' ) );
                             payload.SerializeToString( cert.mutable_proposal()->mutable_subject()->mutable_payload() );
                         } );
        expect_rejected( "embedded transaction",
                         []( auto &cert )
                         {
                             auto payload = ConsensusManager::DecodeNonceSubject( cert.proposal().subject() ).value();
                             payload.mutable_transaction()->mutable_transfer()->mutable_dag_struct()->set_nonce( 99 );
                             payload.SerializeToString( cert.mutable_proposal()->mutable_subject()->mutable_payload() );
                         } );
        expect_rejected( "proposal registry",
                         []( auto &cert ) { cert.mutable_proposal()->set_registry_cid( "tampered-registry" ); } );
        expect_rejected( "certificate registry",
                         []( auto &cert ) { cert.set_registry_cid( "tampered-registry" ); } );
        expect_rejected( "vote",
                         []( auto &cert ) { cert.mutable_votes( 0 )->set_approve( false ); } );

        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, CanonicalTwoVoteReplayIsByteIdenticalAndIdempotent )
    {
        auto first    = MakeAccount( getPathString() );
        auto second   = MakeAccount( getPathString(), kPrivateKey2 );
        auto registry = MakeRegistry( db_, { first, second } );
        auto manager  = MakeManager( registry, db_, pubs_, first );
        ASSERT_TRUE( first && second && registry && manager );

        auto certificate = MakeTwoVoteCertificate( manager, registry, first, second, true );
        ASSERT_TRUE( certificate.has_value() );
        ASSERT_EQ( certificate.value().votes_size(), 2 );
        EXPECT_LT( certificate.value().votes( 0 ).voter_id(), certificate.value().votes( 1 ).voter_id() );
        EXPECT_EQ( certificate.value().timestamp(),
                   std::max( certificate.value().votes( 0 ).timestamp(),
                             certificate.value().votes( 1 ).timestamp() ) );

        std::vector<ConsensusManager::Vote> reverse_votes{ certificate.value().votes( 1 ),
                                                           certificate.value().votes( 0 ) };
        auto replay = manager->CreateCertificate( certificate.value().proposal(), reverse_votes );
        ASSERT_TRUE( replay.has_value() );
        EXPECT_EQ( replay.value().SerializeAsString(), certificate.value().SerializeAsString() );

        auto unordered = certificate.value();
        unordered.mutable_votes()->SwapElements( 0, 1 );
        ASSERT_TRUE( manager->SubmitCertificate( unordered ).has_value() );
        ASSERT_TRUE( manager->SubmitCertificate( certificate.value() ).has_value() );

        auto slot   = ConsensusManagerTestAccess::Slot( certificate.value() ).value();
        auto stored = manager->GetCertificateBySlotId( slot );
        ASSERT_TRUE( stored.has_value() );
        EXPECT_EQ( stored.value().SerializeAsString(), certificate.value().SerializeAsString() );
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, UnknownFieldVariantsAreRejectedBeforeRemoteMerge )
    {
        auto first    = MakeAccount( getPathString() );
        auto second   = MakeAccount( getPathString(), kPrivateKey2 );
        auto registry = MakeRegistry( db_, { first, second } );
        auto manager  = MakeManager( registry, db_, pubs_, first );
        ASSERT_TRUE( first && second && registry && manager );
        auto certificate = MakeTwoVoteCertificate( manager, registry, first, second );
        ASSERT_TRUE( certificate.has_value() );

        const auto slot   = ConsensusManagerTestAccess::Slot( certificate.value() ).value();
        const auto winner = ConsensusManagerTestAccess::Winner( certificate.value() ).value();
        auto expect_rejected = [&]( ConsensusManager::Certificate mutated )
        {
            EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta(
                manager,
                MakeCertificatePairDelta( mutated ) ) );
            EXPECT_TRUE( db_->Get( { "/cert/v2/slot/" + slot } ).has_error() );
            EXPECT_TRUE( db_->Get( { "/cert/v2/tx/" + winner } ).has_error() );
        };

        auto top_level = certificate.value();
        top_level.GetReflection()->MutableUnknownFields( &top_level )->AddVarint( 100, 1 );
        expect_rejected( std::move( top_level ) );

        auto nested = certificate.value();
        nested.mutable_votes( 0 )
            ->GetReflection()
            ->MutableUnknownFields( nested.mutable_votes( 0 ) )
            ->AddLengthDelimited( 101, "nested" );
        expect_rejected( std::move( nested ) );
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, RedundantFieldAndVoteOrderVariantsAreRejectedRemotely )
    {
        auto first    = MakeAccount( getPathString() );
        auto second   = MakeAccount( getPathString(), kPrivateKey2 );
        auto registry = MakeRegistry( db_, { first, second } );
        auto manager  = MakeManager( registry, db_, pubs_, first );
        ASSERT_TRUE( first && second && registry && manager );
        auto certificate = MakeTwoVoteCertificate( manager, registry, first, second );
        ASSERT_TRUE( certificate.has_value() );

        const auto slot   = ConsensusManagerTestAccess::Slot( certificate.value() ).value();
        const auto winner = ConsensusManagerTestAccess::Winner( certificate.value() ).value();
        auto expect_rejected = [&]( ConsensusManager::Certificate mutated )
        {
            EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta(
                manager,
                MakeCertificatePairDelta( mutated ) ) );
            EXPECT_TRUE( db_->Get( { "/cert/v2/slot/" + slot } ).has_error() );
            EXPECT_TRUE( db_->Get( { "/cert/v2/tx/" + winner } ).has_error() );
        };

        auto changed_timestamp = certificate.value();
        changed_timestamp.set_timestamp( changed_timestamp.timestamp() + 1 );
        expect_rejected( std::move( changed_timestamp ) );
        auto changed_total = certificate.value();
        changed_total.set_total_weight( changed_total.total_weight() + 1 );
        expect_rejected( std::move( changed_total ) );
        auto changed_approved = certificate.value();
        changed_approved.set_approved_weight( changed_approved.approved_weight() + 1 );
        expect_rejected( std::move( changed_approved ) );
        auto reverse_order = certificate.value();
        reverse_order.mutable_votes()->SwapElements( 0, 1 );
        expect_rejected( std::move( reverse_order ) );
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, DuplicateVoteAndInvalidVoteVariantsAreRejected )
    {
        auto first    = MakeAccount( getPathString() );
        auto second   = MakeAccount( getPathString(), kPrivateKey2 );
        auto outsider = MakeAccount( getPathString(), kPrivateKey3 );
        auto registry = MakeRegistry( db_, { first, second } );
        auto manager  = MakeManager( registry, db_, pubs_, first );
        ASSERT_TRUE( first && second && outsider && registry && manager );
        auto certificate = MakeTwoVoteCertificate( manager, registry, first, second );
        ASSERT_TRUE( certificate.has_value() );

        const auto slot   = ConsensusManagerTestAccess::Slot( certificate.value() ).value();
        const auto winner = ConsensusManagerTestAccess::Winner( certificate.value() ).value();
        auto expect_rejected = [&]( ConsensusManager::Certificate mutated )
        {
            EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta(
                manager,
                MakeCertificatePairDelta( mutated ) ) );
            EXPECT_TRUE( db_->Get( { "/cert/v2/slot/" + slot } ).has_error() );
            EXPECT_TRUE( db_->Get( { "/cert/v2/tx/" + winner } ).has_error() );
        };

        auto duplicate = certificate.value();
        *duplicate.add_votes() = duplicate.votes( 0 );
        expect_rejected( std::move( duplicate ) );

        auto wrong_proposal = certificate.value();
        wrong_proposal.mutable_votes( 0 )->set_proposal_id( std::string( 64, '0' ) );
        expect_rejected( std::move( wrong_proposal ) );

        auto bad_signature = certificate.value();
        bad_signature.mutable_votes( 0 )->set_signature( "invalid" );
        expect_rejected( std::move( bad_signature ) );

        auto unknown_voter = certificate.value();
        auto *unknown_vote = unknown_voter.add_votes();
        unknown_vote->set_proposal_id( unknown_voter.proposal_id() );
        unknown_vote->set_voter_id( outsider->GetAddress() );
        unknown_vote->set_approve( true );
        unknown_vote->set_timestamp( unknown_voter.timestamp() );
        auto unknown_signing = ConsensusManager::VoteSigningBytes( *unknown_vote );
        ASSERT_TRUE( unknown_signing.has_value() );
        auto unknown_signature = outsider->Sign( unknown_signing.value() );
        unknown_vote->set_signature( unknown_signature.data(), unknown_signature.size() );
        expect_rejected( std::move( unknown_voter ) );

        auto appended_invalid = certificate.value();
        auto *invalid_vote = appended_invalid.add_votes();
        invalid_vote->set_proposal_id( appended_invalid.proposal_id() );
        invalid_vote->set_voter_id( outsider->GetAddress() );
        invalid_vote->set_approve( true );
        invalid_vote->set_timestamp( appended_invalid.timestamp() );
        invalid_vote->set_signature( "invalid" );
        expect_rejected( std::move( appended_invalid ) );
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, InactiveVoteIsRejectedByStrictTally )
    {
        auto first    = MakeAccount( getPathString() );
        auto second   = MakeAccount( getPathString(), kPrivateKey2 );
        auto registry = MakeRegistry( db_, { first, second } );
        auto manager  = MakeManager( registry, db_, pubs_, first );
        ASSERT_TRUE( first && second && registry && manager );
        auto certificate = MakeTwoVoteCertificate( manager, registry, first, second );
        ASSERT_TRUE( certificate.has_value() );

        auto registry_snapshot = registry->LoadRegistryByCid( registry->GetRegistryCid() );
        ASSERT_TRUE( registry_snapshot.has_value() );
        for ( auto &entry : *registry_snapshot.value().mutable_validators() )
        {
            if ( entry.validator_id() == second->GetAddress() )
            {
                entry.set_status( ValidatorRegistry::Status::SUSPENDED );
            }
        }
        const std::vector<ConsensusManager::Vote> votes( certificate.value().votes().begin(),
                                                         certificate.value().votes().end() );
        auto tally = manager->TallyVotes( certificate.value().proposal(),
                                          votes,
                                          registry_snapshot.value(),
                                          registry->GetRegistryCid() );
        EXPECT_TRUE( tally.has_error() );
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, RuntimeNamespaceRejectsLegacyAndMalformedElements )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );

        const std::vector<std::string> rejected_keys{
            "/cert/" + std::string( kTx ),
            "/cert/v1/slot/" + std::string( kSlot ),
            "/cert/v2/slot/not-a-canonical-slot",
            "/cert/v2/tx/" + std::string( kTx ) + "/extra",
        };
        for ( const auto &key : rejected_keys )
        {
            SCOPED_TRACE( key );
            crdt::pb::Delta delta;
            auto           *element = delta.add_elements();
            element->set_key( key );
            element->set_value( "poison" );
            EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta( manager, delta ) );
            EXPECT_TRUE( db_->Get( { key } ).has_error() );
        }

        crdt::CRDTDataFilter runtime_filter( db_->GetWorkJournal() );
        ASSERT_TRUE( runtime_filter.RegisterDeltaFilter(
            "^/?cert/.*$",
            [manager]( const crdt::pb::Delta &delta )
            { return ConsensusManagerTestAccess::FilterDelta( manager, delta ); } ) );
        crdt::pb::Delta mixed;
        auto           *legacy = mixed.add_elements();
        legacy->set_key( "/cert/" + std::string( kTx ) );
        legacy->set_value( "legacy" );
        auto *unrelated = mixed.add_elements();
        unrelated->set_key( "/unrelated/runtime-value" );
        unrelated->set_value( "preserved" );
        runtime_filter.FilterElementsOnDelta( mixed );
        ASSERT_EQ( mixed.elements_size(), 1 );
        EXPECT_EQ( mixed.elements( 0 ).key(), "/unrelated/runtime-value" );
        EXPECT_EQ( mixed.elements( 0 ).value(), "preserved" );

        manager->Close();
        manager.reset();
        auto restarted = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( restarted );
        restarted->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, RestartAfterRejectedLegacyRuntimeDeliverySucceeds )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );

        crdt::pb::Delta legacy;
        legacy.add_elements()->set_key( "/cert/v1/" + std::string( kTx ) );
        EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta( manager, legacy ) );
        EXPECT_TRUE( db_->Get( { "/cert/v1/" + std::string( kTx ) } ).has_error() );

        manager->Close();
        manager.reset();
        auto restarted = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( restarted );
        restarted->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, RuntimeNamespaceTombstonesAreTerminallyRejected )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        auto certificate = MakeCertificate( manager, registry, account, account );
        ASSERT_TRUE( certificate.has_value() );

        const auto valid = MakeCertificatePairDelta( certificate.value() );
        const auto slot_key = valid.elements( 0 ).key();

        crdt::pb::Delta tombstone_only;
        auto           *slot_tombstone = tombstone_only.add_tombstones();
        slot_tombstone->set_key( slot_key );
        slot_tombstone->set_id( "exact-element-id" );
        EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta( manager, tombstone_only ) );

        auto mixed = valid;
        auto *index_tombstone = mixed.add_tombstones();
        index_tombstone->set_key( valid.elements( 1 ).key() );
        index_tombstone->set_id( "exact-index-id" );
        EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta( manager, mixed ) );

        crdt::pb::Delta malformed_tombstone;
        malformed_tombstone.add_tombstones()->set_key( "/cert/v2/malformed" );
        EXPECT_FALSE( ConsensusManagerTestAccess::FilterDelta( manager, malformed_tombstone ) );
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, RegistryDependencyStalledPairReturnsRetryDependency )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );

        crdt::GlobalDB::Buffer dependency_payload;
        dependency_payload.put( "not-a-registry" );
        ASSERT_OUTCOME_SUCCESS(
            missing_registry_root,
            db_->Put( { "/dependency/not-registry" }, dependency_payload, {} ) );
        const auto missing_registry_cid = missing_registry_root.toString().value();

        ASSERT_OUTCOME_SUCCESS(
            subject,
            ConsensusManager::CreateNonceSubject(
                account->GetAddress(),
                7,
                "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
                EmbeddedTransaction{},
                MakeCommitment(),
                UTXOWitness{} ) );
        ASSERT_OUTCOME_SUCCESS(
            proposal,
            ConsensusManager::CreateProposal(
                subject,
                account->GetAddress(),
                missing_registry_cid,
                registry->GetRegistryEpoch(),
                [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } ) );
        ASSERT_OUTCOME_SUCCESS(
            vote,
            manager->CreateVote(
                proposal.proposal_id(),
                account->GetAddress(),
                true,
                [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } ) );

        ConsensusManager::Certificate certificate;
        certificate.set_proposal_id( proposal.proposal_id() );
        *certificate.mutable_proposal() = proposal;
        *certificate.add_votes() = vote;
        certificate.set_registry_cid( missing_registry_cid );
        certificate.set_registry_epoch( registry->GetRegistryEpoch() );
        certificate.set_total_weight( 50000 );
        certificate.set_approved_weight( 50000 );
        certificate.set_timestamp( vote.timestamp() );

        auto delta  = MakeCertificatePairDelta( certificate );
        auto result = ConsensusManagerTestAccess::FilterDeltaResult( manager, delta );
        EXPECT_EQ( result.decision, crdt::DeltaFilterDecision::RetryDependency );
        ASSERT_TRUE( result.dependency_cid.has_value() );
        EXPECT_EQ( *result.dependency_cid, missing_registry_cid );

        delta.mutable_elements( 0 )->mutable_value()->back() ^= 0x01;
        auto invalid = ConsensusManagerTestAccess::FilterDeltaResult( manager, delta );
        EXPECT_EQ( invalid.decision, crdt::DeltaFilterDecision::Reject );
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, ExactIdCertificateTombstonesAreStrippedAndRestartSafe )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        ASSERT_OUTCOME_SUCCESS( certificate, MakeCertificate( manager, registry, account, account ) );
        ASSERT_TRUE( manager->SubmitCertificate( certificate ).has_value() );

        const auto slot   = ConsensusManagerTestAccess::Slot( certificate ).value();
        const auto winner = ConsensusManagerTestAccess::Winner( certificate ).value();
        const std::array<std::string, 2> protected_keys{
            "/cert/v2/slot/" + slot,
            "/cert/v2/tx/" + winner,
        };

        for ( std::size_t attack = 0; attack < protected_keys.size(); ++attack )
        {
            SCOPED_TRACE( protected_keys[attack] );
            auto crdt_store = db_->GetCRDTDataStore();
            ASSERT_TRUE( crdt_store );

            const auto unrelated_key = "/unrelated/tombstone-" + std::to_string( attack );
            crdt::GlobalDB::Buffer unrelated_value;
            unrelated_value.put( "live" );
            ASSERT_TRUE( db_->Put( { unrelated_key }, unrelated_value, {} ).has_value() );
            ASSERT_OUTCOME_SUCCESS( unrelated_remove, crdt_store->CreateDeltaToRemove( unrelated_key ) );
            ASSERT_OUTCOME_SUCCESS( certificate_remove,
                                    crdt_store->CreateDeltaToRemove( protected_keys[attack] ) );
            ASSERT_EQ( certificate_remove->tombstones_size(), 1 );
            unrelated_remove->add_tombstones()->CopyFrom( certificate_remove->tombstones( 0 ) );

            crdt::CRDTDataFilter runtime_filter( db_->GetWorkJournal() );
            ASSERT_TRUE( runtime_filter.RegisterDeltaFilter(
                "^/?cert/.*$",
                [manager]( const crdt::pb::Delta &delta )
                { return ConsensusManagerTestAccess::FilterDeltaResult( manager, delta ); } ) );
            auto sanitized = runtime_filter.FilterDelta( *unrelated_remove );
            EXPECT_EQ( sanitized.decision, crdt::DeltaFilterDecision::Reject );
            ASSERT_EQ( sanitized.delta.tombstones_size(), 1 );
            EXPECT_EQ( sanitized.delta.tombstones( 0 ).key(), unrelated_key );

            ASSERT_TRUE(
                crdt_store->Publish( std::make_shared<crdt::pb::Delta>( sanitized.delta ), {} ).has_value() );
            EXPECT_TRUE( db_->Get( { unrelated_key } ).has_error() );
            ASSERT_TRUE( manager->GetCertificateBySlotId( slot ).has_value() );
            ASSERT_TRUE( manager->GetCertificateBySubjectHash( winner ).has_value() );

            manager->Close();
            manager.reset();
            manager = MakeManager( registry, db_, pubs_, account );
            ASSERT_TRUE( manager );
            ASSERT_TRUE( manager->GetCertificateBySlotId( slot ).has_value() );
            ASSERT_TRUE( manager->GetCertificateBySubjectHash( winner ).has_value() );
        }
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, LegacyCertificateStateRejectsStartupBeforeSideEffects )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( account && registry );

        const auto legacy_key = "/cert/" + std::string( kTx );
        ASSERT_TRUE( PutRawCertificateState( db_, legacy_key, "legacy-certificate" ).has_value() );
        ASSERT_TRUE( db_->Get( { legacy_key } ).has_value() );

        auto manager = MakeManager( registry, db_, pubs_, account );
        EXPECT_FALSE( manager );

        // A failed startup must not have installed the v2 pair filter. This
        // post-failure probe is deliberately partial and remains locally visible.
        const auto probe_key = "/cert/v2/slot/" + std::string( kSlot );
        ASSERT_TRUE( PutRawCertificateState( db_, probe_key, "startup-side-effect-probe" ).has_value() );
        EXPECT_TRUE( db_->Get( { probe_key } ).has_value() );
    }

    TEST_F( ConsensusCertificateStoreTest, StartupAcceptsEmptyCertificateState )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager  = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        manager->Close();
    }

    TEST_F( ConsensusCertificateStoreTest, StartupAcceptsV2OnlyCertificateState )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( account && registry );

        auto creator = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( creator );
        ASSERT_OUTCOME_SUCCESS( certificate, MakeCertificate( creator, registry, account, account ) );
        ASSERT_OUTCOME_SUCCESS( slot, ConsensusManagerTestAccess::Slot( certificate ) );
        ASSERT_OUTCOME_SUCCESS( winner, ConsensusManagerTestAccess::Winner( certificate ) );
        creator->Close();

        ASSERT_TRUE( PutRawCertificateState(
                         db_, "/cert/v2/slot/" + slot, certificate.SerializeAsString() )
                         .has_value() );
        ASSERT_TRUE( PutRawCertificateState( db_, "/cert/v2/tx/" + winner, slot ).has_value() );

        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );
        manager->Close();
    }
} // namespace sgns::test
