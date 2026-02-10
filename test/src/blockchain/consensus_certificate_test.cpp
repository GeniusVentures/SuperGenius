#include <gtest/gtest.h>

#define private public
#include "blockchain/Consensus.hpp"
#undef private

#include "account/GeniusAccount.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace
{
    constexpr const char *kTestPrivateKey = "0x4f3edf983ac636a65a842ce7c78d9aa706d3b113bce8b1a6f0d4f3b9b7f0a1b2";

    std::shared_ptr<sgns::GeniusAccount> MakeAccount( const std::string &path )
    {
        auto account = sgns::GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ), kTestPrivateKey, path, false );
        EXPECT_TRUE( account );
        return account;
    }

    std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry( const std::shared_ptr<sgns::crdt::GlobalDB> &db,
                                                           const std::shared_ptr<sgns::GeniusAccount>  &account )
    {
        using sgns::ValidatorRegistry;
        auto registry = ValidatorRegistry::New(
            db,
            1,
            1,
            ValidatorRegistry::WeightConfig{},
            account->GetAddress(),
            []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
            { cb( outcome::failure( std::errc::not_supported ) ); } );
        EXPECT_TRUE( registry );

        auto store_result = registry->StoreGenesisRegistry( account->GetAddress(),
                                                            [account]( std::vector<uint8_t> payload )
                                                            { return account->Sign( std::move( payload ) ); } );
        EXPECT_FALSE( store_result.has_error() );

        ASSERT_WAIT_FOR_CONDITION(
            [&registry]()
            {
                auto load = registry->LoadRegistry();
                return load.has_value() && !registry->GetRegistryCid().empty();
            },
            std::chrono::milliseconds( 2000 ),
            "registry initialized",
            nullptr );

        return registry;
    }
}

namespace sgns::test
{
    class ConsensusCertificateTest : public ::test::CRDTFixture
    {
    public:
        ConsensusCertificateTest() : CRDTFixture( "ConsensusCertificateTest" ) {}

        static void SetUpTestSuite()
        {
            CRDTFixture::SetUpTestSuite();
        }
    };

    TEST_F( ConsensusCertificateTest, CreateCertificateEmbedsProposal )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );

        auto manager = ConsensusManager::New(
            registry,
            db_,
            pubs_,
            [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
            account->GetAddress() );

        std::string tx_hash        = "0x010203";
        auto        subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(), 1, tx_hash );
        ASSERT_TRUE( subject_result.has_value() );

        auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                        account->GetAddress(),
                                                        registry->GetRegistryCid(),
                                                        registry->GetRegistryEpoch() );
        ASSERT_TRUE( proposal_result.has_value() );

        auto vote_result = manager->CreateVote( proposal_result.value().proposal_id(),
                                                account->GetAddress(),
                                                true,
                                                [account]( std::vector<uint8_t> payload )
                                                { return account->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( vote_result.has_value() );

        auto cert_result = manager->CreateCertificate( proposal_result.value(), { vote_result.value() } );
        ASSERT_TRUE( cert_result.has_value() );

        const auto &cert = cert_result.value();
        EXPECT_TRUE( cert.has_proposal() );
        EXPECT_EQ( cert.proposal().proposal_id(), proposal_result.value().proposal_id() );
    }

    TEST_F( ConsensusCertificateTest, HandleCertificateRejectsMismatchedProposal )
    {
        auto account  = MakeAccount( getPathString() );
        auto registry = MakeRegistry( db_, account );

        auto manager = ConsensusManager::New(
            registry,
            db_,
            pubs_,
            [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
            account->GetAddress() );

        std::string tx_hash        = "0x010203";
        auto        subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(), 7, tx_hash );
        ASSERT_TRUE( subject_result.has_value() );

        auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                        account->GetAddress(),
                                                        registry->GetRegistryCid(),
                                                        registry->GetRegistryEpoch(),
                                                        [account]( std::vector<uint8_t> payload )
                                                        { return account->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( proposal_result.has_value() );

        auto vote_result = manager->CreateVote( proposal_result.value().proposal_id(),
                                                account->GetAddress(),
                                                true,
                                                [account]( std::vector<uint8_t> payload )
                                                { return account->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( vote_result.has_value() );

        auto cert_result = manager->CreateCertificate( proposal_result.value(), { vote_result.value() } );
        ASSERT_TRUE( cert_result.has_value() );

        auto cert = cert_result.value();

        bool notified = false;
        manager->SetCertificateCallback( [&notified]( const ConsensusProposal &, const ConsensusCertificate & )
                                         { notified = true; } );

        manager->HandleCertificate( cert );
        EXPECT_TRUE( notified );

        notified          = false;
        auto *bad_subject = cert.mutable_proposal()->mutable_subject()->mutable_nonce();
        bad_subject->set_nonce( bad_subject->nonce() + 1 );

        manager->HandleCertificate( cert );
        EXPECT_FALSE( notified );
    }
} // namespace sgns::test
