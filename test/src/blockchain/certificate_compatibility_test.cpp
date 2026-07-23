#include <gtest/gtest.h>

#include <thread>

#include "account/GeniusAccount.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
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
        bootstrap->Close();
        ASSERT_TRUE(
            PutRaw( db_, "/cert/v2/slot/" + std::string( kOtherCanonicalHash ), serialized ).has_value() );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );
        ExpectError( manager->GetCertificateBySlotId( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::IntegrityError );
        manager->Close();
    }

    TEST_F( CertificateCompatibilityTest, SlotLookupRejectsMalformedCertificateAsIntegrityError )
    {
        auto account = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( account && registry );
        ASSERT_TRUE(
            PutRaw( db_, "/cert/v2/slot/" + std::string( kOtherCanonicalHash ), "not-a-certificate" ).has_value() );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );
        ExpectError( manager->GetCertificateBySlotId( std::string( kOtherCanonicalHash ) ),
                     ConsensusManager::CertificateStoreError::IntegrityError );
        manager->Close();
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
} // namespace sgns::test
