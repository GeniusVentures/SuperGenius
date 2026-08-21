/**
 * @file       validator_registry_certificate_lookup_test.cpp
 * @brief      Regression tests for registry batch certificate slot authority.
 */

#include <gtest/gtest.h>

#include "account/GeniusAccount.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace sgns
{
    class ValidatorRegistryCertificateLookupTestAccess
    {
    public:
        static outcome::result<ConsensusCertificate> LoadForPendingSubject(
            const std::shared_ptr<ValidatorRegistry> &registry, const std::string &subject_hash )
        {
            auto slot_result = registry->GetPendingCertificateSlot( subject_hash );
            if ( slot_result.has_error() )
            {
                return outcome::failure( slot_result.error() );
            }
            return registry->LoadCertificateBySlot( slot_result.value() );
        }

        static outcome::result<std::string> PendingSlot( const std::shared_ptr<ValidatorRegistry> &registry,
                                                          const std::string                        &subject_hash )
        {
            return registry->GetPendingCertificateSlot( subject_hash );
        }
    };
} // namespace sgns

using namespace sgns;

namespace
{
    constexpr const char *kPrivateKey = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    class ValidatorRegistryCertificateLookupTest : public ::test::CRDTFixture
    {
    public:
        ValidatorRegistryCertificateLookupTest() : ::test::CRDTFixture( "validator_registry_certificate_lookup_test" )
        {
        }

    protected:
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
        }

        std::shared_ptr<GeniusAccount> MakeAccount()
        {
            auto account = GeniusAccount::NewFromPrivateKey(
                TokenID::FromBytes( { 0x00 } ), kPrivateKey, getPathString(), false );
            EXPECT_TRUE( account );
            return account;
        }

        std::shared_ptr<ValidatorRegistry> MakeRegistry( const std::shared_ptr<GeniusAccount> &account )
        {
            auto registry = ValidatorRegistry::New(
                db_,
                1,
                1,
                ValidatorRegistry::WeightConfig{},
                account->GetAddress(),
                []( const std::string &, std::function<void( outcome::result<std::string> )> callback )
                { callback( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );
            if ( !registry )
            {
                return nullptr;
            }

            auto stored = registry->StoreGenesisRegistry(
                account->GetAddress(), [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
            EXPECT_TRUE( stored.has_value() );
            ASSERT_WAIT_FOR_CONDITION(
                [&registry]()
                {
                    auto current = registry->LoadCurrentRegistry();
                    return current.has_value() && !registry->GetRegistryCid().empty();
                },
                std::chrono::milliseconds( 2000 ),
                "registry initialized",
                nullptr );
            return registry;
        }

        std::shared_ptr<ConsensusManager> MakeManager( const std::shared_ptr<ValidatorRegistry> &registry,
                                                        const std::shared_ptr<GeniusAccount>     &account )
        {
            auto manager = ConsensusManager::New(
                registry,
                db_,
                pubs_,
                [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
                account->GetAddress() );
            EXPECT_TRUE( manager );
            return manager;
        }

        outcome::result<ConsensusCertificate> MakeCertificate( const std::shared_ptr<ConsensusManager>  &manager,
                                                                 const std::shared_ptr<ValidatorRegistry> &registry,
                                                                 const std::shared_ptr<GeniusAccount>     &account,
                                                                 const std::string                        &subject_hash,
                                                                 uint64_t                                  nonce )
        {
            auto subject = ConsensusManager::CreateNonceSubject(
                account->GetAddress(), nonce, subject_hash, EmbeddedTransaction{}, std::nullopt, std::nullopt );
            if ( subject.has_error() )
            {
                return outcome::failure( subject.error() );
            }
            auto proposal = manager->CreateProposal(
                subject.value(), account->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
            if ( proposal.has_error() )
            {
                return outcome::failure( proposal.error() );
            }
            auto vote = manager->CreateVote(
                proposal.value().proposal_id(),
                account->GetAddress(),
                true,
                [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
            if ( vote.has_error() )
            {
                return outcome::failure( vote.error() );
            }
            return manager->CreateCertificate( proposal.value(), { vote.value() } );
        }

        void WriteCertificate( const std::string &slot, const ConsensusCertificate &certificate )
        {
            std::string serialized;
            ASSERT_TRUE( certificate.SerializeToString( &serialized ) );
            crdt::GlobalDB::Buffer value;
            value.put( serialized );
            ASSERT_TRUE( db_->Put( { std::string( "/cert/" ) + slot }, value, {} ).has_value() );
        }

        void WriteRaw( const std::string &slot, const std::string &bytes )
        {
            crdt::GlobalDB::Buffer value;
            value.put( bytes );
            ASSERT_TRUE( db_->Put( { std::string( "/cert/" ) + slot }, value, {} ).has_value() );
        }
    };
} // namespace

TEST_F( ValidatorRegistryCertificateLookupTest, LoadsPendingCertificateOnlyFromItsCanonicalSlot )
{
    auto account  = MakeAccount();
    auto registry = MakeRegistry( account );
    auto manager  = MakeManager( registry, account );
    ASSERT_TRUE( manager );

    const std::string subject_hash = "registry-batch-subject-success";
    auto certificate = MakeCertificate( manager, registry, account, subject_hash, 100 );
    ASSERT_TRUE( certificate.has_value() );
    ASSERT_TRUE( registry->OnFinalizedCertificate( certificate.value() ).has_value() );

    auto slot_result = sgns::ValidatorRegistryCertificateLookupTestAccess::PendingSlot( registry, subject_hash );
    ASSERT_TRUE( slot_result.has_value() );
    const auto slot = slot_result.value();
    ASSERT_NE( slot, subject_hash );
    WriteCertificate( slot, certificate.value() );

    auto loaded = sgns::ValidatorRegistryCertificateLookupTestAccess::LoadForPendingSubject( registry, subject_hash );
    ASSERT_TRUE( loaded.has_value() );
    EXPECT_EQ( loaded.value().proposal_id(), certificate.value().proposal_id() );
    manager->Close();
}

TEST_F( ValidatorRegistryCertificateLookupTest, RejectsLegacyMalformedMismatchedAndUnavailableSlotEvidence )
{
    auto account  = MakeAccount();
    auto registry = MakeRegistry( account );
    auto manager  = MakeManager( registry, account );
    ASSERT_TRUE( manager );

    const std::string subject_hash = "registry-batch-subject-negative";
    auto certificate = MakeCertificate( manager, registry, account, subject_hash, 101 );
    ASSERT_TRUE( certificate.has_value() );
    ASSERT_TRUE( registry->OnFinalizedCertificate( certificate.value() ).has_value() );
    auto slot_result = sgns::ValidatorRegistryCertificateLookupTestAccess::PendingSlot( registry, subject_hash );
    ASSERT_TRUE( slot_result.has_value() );
    const auto slot = slot_result.value();

    // A valid legacy subject-hash key never becomes an authority record.
    WriteCertificate( subject_hash, certificate.value() );
    EXPECT_FALSE(
        sgns::ValidatorRegistryCertificateLookupTestAccess::LoadForPendingSubject( registry, subject_hash ).has_value() );

    WriteRaw( slot, "not-a-certificate" );
    EXPECT_FALSE(
        sgns::ValidatorRegistryCertificateLookupTestAccess::LoadForPendingSubject( registry, subject_hash ).has_value() );

    const std::string other_subject_hash = "registry-batch-subject-other";
    auto other = MakeCertificate( manager, registry, account, other_subject_hash, 102 );
    ASSERT_TRUE( other.has_value() );
    WriteCertificate( slot, other.value() );
    EXPECT_FALSE(
        sgns::ValidatorRegistryCertificateLookupTestAccess::LoadForPendingSubject( registry, subject_hash ).has_value() );

    EXPECT_FALSE( sgns::ValidatorRegistryCertificateLookupTestAccess::PendingSlot(
                      registry, "missing-pending-subject-association" )
                      .has_value() );
    manager->Close();
}
