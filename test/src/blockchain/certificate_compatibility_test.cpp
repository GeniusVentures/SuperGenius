#include <gtest/gtest.h>

#include <thread>

#include "account/GeniusAccount.hpp"
#include "account/GeniusTransaction.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "storage/database_error.hpp"
#include "testutil/storage/base_crdt_test.hpp"

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
    };
} // namespace sgns

namespace sgns::test
{
    namespace
    {
        constexpr const char *kPrivateKey =
            "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
        constexpr std::string_view kOtherCanonicalHash =
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

        std::shared_ptr<GeniusAccount> MakeAccount( const std::string &path )
        {
            return GeniusAccount::NewFromPrivateKey(
                TokenID::FromBytes( { 0x00 } ),
                kPrivateKey,
                boost::filesystem::path( path ) / "compatibility-account",
                false );
        }

        std::shared_ptr<ValidatorRegistry> MakeRegistry( const std::shared_ptr<crdt::GlobalDB> &db,
                                                         const std::shared_ptr<GeniusAccount>  &account )
        {
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
            if ( registry
                     ->StoreGenesisRegistry(
                         { account->GetAddress() },
                         [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } )
                     .has_error() )
            {
                return nullptr;
            }
            for ( int attempt = 0; attempt < 100; ++attempt )
            {
                if ( registry->LoadCurrentRegistry().has_value() && !registry->GetRegistryCid().empty() )
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
            const std::shared_ptr<GeniusAccount>     &account )
        {
            BOOST_OUTCOME_TRY(
                auto subject,
                ConsensusManager::CreateNonceSubject(
                    account->GetAddress(),
                    7,
                    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
                    EmbeddedTransaction{},
                    MakeCommitment(),
                    UTXOWitness{} ) );
            BOOST_OUTCOME_TRY(
                auto proposal,
                ConsensusManager::CreateProposal(
                    subject,
                    account->GetAddress(),
                    registry->GetRegistryCid(),
                    registry->GetRegistryEpoch(),
                    [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } ) );
            BOOST_OUTCOME_TRY(
                auto vote,
                manager->CreateVote(
                    proposal.proposal_id(),
                    account->GetAddress(),
                    true,
                    [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } ) );
            return manager->CreateCertificate( proposal, { vote } );
        }

        outcome::result<CID> PutRaw( const std::shared_ptr<crdt::GlobalDB> &db,
                                     const std::string                     &key,
                                     std::string_view                       value )
        {
            crdt::GlobalDB::Buffer buffer;
            buffer.put( value );
            return db->Put( { key }, buffer, {} );
        }

        void ExpectError( const outcome::result<ConsensusManager::Certificate> &result,
                          ConsensusManager::CertificateStoreError                expected )
        {
            ASSERT_TRUE( result.has_error() );
            EXPECT_EQ( result.error(), make_error_code( expected ) );
        }

        void InjectReadError( const std::shared_ptr<ConsensusManager> &manager,
                              storage::DatabaseError                    error )
        {
            ConsensusManagerTestAccess::SetCertificateReader(
                manager,
                [error]( const crdt::HierarchicalKey & )
                    -> outcome::result<crdt::GlobalDB::Buffer>
                { return outcome::failure( error ); } );
        }
    } // namespace

    class CertificateCompatibilityTest : public ::test::CRDTFixture
    {
    public:
        CertificateCompatibilityTest() : CRDTFixture( "CertificateCompatibilityTest" )
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
        }
    };

    TEST_F( CertificateCompatibilityTest, SlotLookupReturnsValidatedAuthoritativeCertificate )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        auto certificate = MakeCertificate( manager, registry, account );
        ASSERT_TRUE( certificate.has_value() );
        ASSERT_TRUE( manager->SubmitCertificate( certificate.value() ).has_value() );

        auto slot = ConsensusManagerTestAccess::Slot( certificate.value() );
        ASSERT_TRUE( slot.has_value() );
        auto loaded = manager->GetCertificateBySlotId( slot.value() );
        ASSERT_TRUE( loaded.has_value() );
        EXPECT_EQ( loaded.value().SerializeAsString(), certificate.value().SerializeAsString() );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, SlotLookupNotFoundIsTyped )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        ExpectError( manager->GetCertificateBySlotId( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::NotFound );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, SlotReadErrorCorruptionIsIntegrityError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        InjectReadError( manager, storage::DatabaseError::CORRUPTION );
        ExpectError( manager->GetCertificateBySlotId( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::IntegrityError );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, SlotReadErrorOperationalIsStorageError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        InjectReadError( manager, storage::DatabaseError::IO_ERROR );
        ExpectError( manager->GetCertificateBySlotId( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::StorageError );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, SlotLookupRejectsWrongSlotKeyAsIntegrityError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto bootstrap = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && bootstrap );
        auto certificate = MakeCertificate( bootstrap, registry, account );
        ASSERT_TRUE( certificate.has_value() );
        std::string serialized;
        ASSERT_TRUE( certificate.value().SerializeToString( &serialized ) );
        ASSERT_TRUE(
            PutRaw( db_, "/cert/v2/slot/" + std::string( kOtherCanonicalHash ), serialized ).has_value() );
        ExpectError( bootstrap->GetCertificateBySlotId( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::IntegrityError );
        bootstrap->Close();

        // Strict restoration rejects corrupt authority before participation.
        auto manager = MakeManager( registry, db_, pubs_, account );
        EXPECT_FALSE( manager );
    }

    TEST_F( CertificateCompatibilityTest, SlotLookupRejectsMalformedCertificateAsIntegrityError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto bootstrap = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && bootstrap );
        ASSERT_TRUE(
            PutRaw( db_, "/cert/v2/slot/" + std::string( kOtherCanonicalHash ), "not-a-certificate" ).has_value() );
        ExpectError( bootstrap->GetCertificateBySlotId( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::IntegrityError );
        bootstrap->Close();

        // Strict restoration rejects corrupt authority before participation.
        auto manager = MakeManager( registry, db_, pubs_, account );
        EXPECT_FALSE( manager );
    }

    TEST_F( CertificateCompatibilityTest, HashLookupReturnsWinnerThroughVerifiedIndex )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        auto certificate = MakeCertificate( manager, registry, account );
        ASSERT_TRUE( certificate.has_value() );
        ASSERT_TRUE( manager->SubmitCertificate( certificate.value() ).has_value() );
        auto winner = ConsensusManagerTestAccess::Winner( certificate.value() );
        ASSERT_TRUE( winner.has_value() );

        auto loaded = manager->GetCertificateBySubjectHash( winner.value() );
        ASSERT_TRUE( loaded.has_value() );
        EXPECT_EQ( loaded.value().SerializeAsString(), certificate.value().SerializeAsString() );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, HashLookupAbsentIndexIsNotFound )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        ExpectError( manager->GetCertificateBySubjectHash( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::NotFound );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, IndexReadErrorCorruptionIsIntegrityError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        InjectReadError( manager, storage::DatabaseError::CORRUPTION );
        ExpectError( manager->GetCertificateBySubjectHash( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::IntegrityError );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, IndexReadErrorOperationalIsStorageError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        InjectReadError( manager, storage::DatabaseError::IO_ERROR );
        ExpectError( manager->GetCertificateBySubjectHash( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::StorageError );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, HashLookupMalformedIndexIsIntegrityError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( account && registry );
        ASSERT_TRUE(
            PutRaw( db_, "/cert/v2/tx/" + std::string( kOtherCanonicalHash ), "malformed-slot" ).has_value() );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );
        ExpectError( manager->GetCertificateBySubjectHash( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::IntegrityError );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, MalformedIndexReturnsTypedDiagnosticWithoutMutatingAuthority )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        auto certificate = MakeCertificate( manager, registry, account );
        ASSERT_TRUE( certificate.has_value() );
        ASSERT_TRUE( manager->SubmitCertificate( certificate.value() ).has_value() );
        auto slot = ConsensusManagerTestAccess::Slot( certificate.value() );
        auto winner = ConsensusManagerTestAccess::Winner( certificate.value() );
        ASSERT_TRUE( slot && winner );
        const auto slot_key = "/cert/v2/slot/" + slot.value();
        const auto index_key = "/cert/v2/tx/" + winner.value();
        const auto authoritative_before = db_->Get( { slot_key } );
        const auto index_before = db_->Get( { index_key } );
        ASSERT_TRUE( authoritative_before && index_before );

        ConsensusManagerTestAccess::SetCertificateReader(
            manager,
            [db = db_, index_key]( const crdt::HierarchicalKey &key )
                -> outcome::result<crdt::GlobalDB::Buffer>
            {
                if ( key.GetKey() == index_key )
                {
                    crdt::GlobalDB::Buffer malformed;
                    malformed.put( "malformed-slot" );
                    return malformed;
                }
                return db->Get( key );
            } );
        auto corrupt = manager->GetCertificateBySubjectHash( winner.value() );
        ASSERT_TRUE( corrupt.has_error() );
        EXPECT_EQ( corrupt.error(),
                   make_error_code( ConsensusManager::CertificateStoreError::IntegrityError ) );
        EXPECT_FALSE( corrupt.error().message().empty() );
        ConsensusManagerTestAccess::ResetCertificateReader( manager );

        const auto authoritative_after = db_->Get( { slot_key } );
        const auto index_after = db_->Get( { index_key } );
        ASSERT_TRUE( authoritative_after && index_after );
        EXPECT_EQ( authoritative_after.value().toString(), authoritative_before.value().toString() );
        EXPECT_EQ( index_after.value().toString(), index_before.value().toString() );
        auto restored = manager->GetCertificateBySubjectHash( winner.value() );
        ASSERT_TRUE( restored.has_value() );
        EXPECT_EQ( restored.value().SerializeAsString(), certificate.value().SerializeAsString() );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, HashLookupMissingSlotIsIntegrityError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( account && registry );
        ASSERT_TRUE( PutRaw( db_,
                            "/cert/v2/tx/" + std::string( kOtherCanonicalHash ),
                            kOtherCanonicalHash )
                         .has_value() );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );
        ExpectError( manager->GetCertificateBySubjectHash( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::IntegrityError );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, DanglingIndexSlotCorruptionIsIntegrityError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( account && registry );
        ASSERT_TRUE( PutRaw( db_,
                            "/cert/v2/tx/" + std::string( kOtherCanonicalHash ),
                            kOtherCanonicalHash )
                         .has_value() );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );
        ConsensusManagerTestAccess::SetCertificateReader(
            manager,
            [db = db_]( const crdt::HierarchicalKey &key )
                -> outcome::result<crdt::GlobalDB::Buffer>
            {
                if ( key.GetKey().find( "/cert/v2/slot/" ) == 0 )
                {
                    return outcome::failure( storage::DatabaseError::CORRUPTION );
                }
                return db->Get( key );
            } );
        ExpectError( manager->GetCertificateBySubjectHash( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::IntegrityError );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, DanglingIndexSlotOperationalFailureIsStorageError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( account && registry );
        ASSERT_TRUE( PutRaw( db_,
                            "/cert/v2/tx/" + std::string( kOtherCanonicalHash ),
                            kOtherCanonicalHash )
                         .has_value() );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );
        ConsensusManagerTestAccess::SetCertificateReader(
            manager,
            [db = db_]( const crdt::HierarchicalKey &key )
                -> outcome::result<crdt::GlobalDB::Buffer>
            {
                if ( key.GetKey().find( "/cert/v2/slot/" ) == 0 )
                {
                    return outcome::failure( storage::DatabaseError::IO_ERROR );
                }
                return db->Get( key );
            } );
        ExpectError( manager->GetCertificateBySubjectHash( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::StorageError );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, HashLookupWinningHashMismatchIsIntegrityError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        auto certificate = MakeCertificate( manager, registry, account );
        ASSERT_TRUE( certificate.has_value() );
        ASSERT_TRUE( manager->SubmitCertificate( certificate.value() ).has_value() );
        auto slot = ConsensusManagerTestAccess::Slot( certificate.value() );
        ASSERT_TRUE( slot.has_value() );
        ASSERT_TRUE( PutRaw( db_,
                            "/cert/v2/tx/" + std::string( kOtherCanonicalHash ),
                            slot.value() )
                         .has_value() );

        ExpectError( manager->GetCertificateBySubjectHash( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::IntegrityError );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, LosingHashIsNotFoundButFullSubjectObservesFinalizedSlot )
    {
        ConsensusManager::RegisterSlotKeyHandler(
            NONCE_SUBJECT_TYPE,
            []( const ConsensusManager::Subject &subject ) -> outcome::result<std::string>
            {
                BOOST_OUTCOME_TRY( auto nonce, ConsensusManager::DecodeNonceSubject( subject ) );
                BOOST_OUTCOME_TRY(
                    auto preimage,
                    GeniusTransaction::MakeNonceSlotPreimage( subject.account_id(), nonce.nonce() ) );
                return GeniusTransaction::HashSlotPreimage( preimage );
            } );
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        auto certificate = MakeCertificate( manager, registry, account );
        ASSERT_TRUE( certificate.has_value() );
        ASSERT_TRUE( manager->SubmitCertificate( certificate.value() ).has_value() );

        constexpr std::string_view losing_hash =
            "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
        auto losing_subject = ConsensusManager::CreateNonceSubject(
            account->GetAddress(),
            7,
            std::string( losing_hash ),
            EmbeddedTransaction{},
            MakeCommitment(),
            UTXOWitness{} );
        ASSERT_TRUE( losing_subject.has_value() );

        ExpectError( manager->GetCertificateBySubjectHash( std::string( losing_hash ) ),
                     ConsensusManager::CertificateStoreError::NotFound );
        EXPECT_TRUE( manager->CheckCertificateForSubject( losing_subject.value() ) );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, PreviousNonceAndProducerHashConsumersResolveWinner )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( account && registry && manager );
        auto certificate = MakeCertificate( manager, registry, account );
        ASSERT_TRUE( certificate.has_value() );
        ASSERT_TRUE( manager->SubmitCertificate( certificate.value() ).has_value() );
        auto winner = ConsensusManagerTestAccess::Winner( certificate.value() );
        ASSERT_TRUE( winner.has_value() );

        auto previous_nonce_certificate = manager->GetCertificateBySubjectHash( winner.value() );
        auto producer_certificate = manager->GetCertificateBySubjectHash( winner.value() );
        ASSERT_TRUE( previous_nonce_certificate.has_value() );
        ASSERT_TRUE( producer_certificate.has_value() );
        EXPECT_EQ( previous_nonce_certificate.value().proposal_id(), certificate.value().proposal_id() );
        EXPECT_EQ( producer_certificate.value().proposal_id(), certificate.value().proposal_id() );
        manager->Close();
    }
} // namespace sgns::test
