#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/GeniusAccount.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt_test_node.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::securecrdt;

    const std::string PREDECESSOR( 64, 'a' );
    const std::string AUTHORIZER( 64, 'b' );
    const std::string SUCCESSOR( 64, 'c' );

    class TwoPartyBarrier
    {
    public:
        void ArriveAndWait()
        {
            std::unique_lock<std::mutex> lock( mutex_ );
            if ( ++arrived_ == 2 )
            {
                released_ = true;
                condition_.notify_all();
                return;
            }
            condition_.wait( lock, [&] { return released_; } );
        }

    private:
        std::mutex              mutex_;
        std::condition_variable condition_;
        size_t                  arrived_  = 0;
        bool                    released_ = false;
    };

    class SecureCrdtCandidateRaceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
            path_                        = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
            constexpr const char *keys[] = {
                "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eab0",
                "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eab1",
            };
            for ( const auto *key : keys )
            {
                signers_.push_back( GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0 } ), key, path_ ) );
            }
            node_ = test::securecrdt::MakeSecureCrdtTestNode( "securecrdt_candidate_race" );
            ASSERT_NE( node_, nullptr );
            secure_crdt_ = std::make_shared<SecureCrdt>( node_->db, "securecrdt_test_topic" );

            authorization_.network_id              = 42;
            authorization_.kind                    = CandidateKind::TrustPolicy;
            authorization_.next_version            = 7;
            authorization_.expected_previous_hash  = PREDECESSOR;
            authorization_.authorizing_policy_hash = AUTHORIZER;
            authorization_.authorized_signers      = { signers_[0]->GetAddress(), signers_[1]->GetAddress() };
            ASSERT_TRUE( secure_crdt_->Registry().RegisterCandidateDomain(
                "trusted-peer",
                CandidateDomainEntry{ "trusted-peer",
                                      CandidateKind::TrustPolicy,
                                      [this]() -> outcome::result<CandidateAuthorizationSnapshot>
                                      { return authorization_; },
                                      &owner_token_ } ) );
            ASSERT_TRUE( secure_crdt_->RegisterFilters() );
        }

        void TearDown() override
        {
            secure_crdt_->Registry().UnregisterCandidateDomainIf( "trusted-peer", &owner_token_ );
            secure_crdt_.reset();
            node_.reset();
            GeniusAccount::SetSecureStorageFactory( nullptr );
            boost::filesystem::remove_all( path_ );
        }

        CandidateApprovalRecord SignedRecord( size_t signer_index, uint8_t payload ) const
        {
            CandidateCore core;
            core.domain                  = "trusted-peer";
            core.network_id              = 42;
            core.kind                    = CandidateKind::TrustPolicy;
            core.version                 = 7;
            core.expected_previous_hash  = PREDECESSOR;
            core.authorizing_policy_hash = AUTHORIZER;
            core.payload                 = { payload };
            const auto bytes             = core.CanonicalBytes();
            EXPECT_TRUE( bytes.has_value() );
            return CandidateApprovalRecord{ CandidateApprovalRecord::ENCODING_VERSION,
                                            core,
                                            signers_[signer_index]->GetAddress(),
                                            signers_[signer_index]->Sign( *bytes ) };
        }

        boost::filesystem::path                               path_;
        int                                                   owner_token_ = 0;
        CandidateAuthorizationSnapshot                        authorization_;
        std::vector<std::shared_ptr<GeniusAccount>>           signers_;
        std::unique_ptr<test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<SecureCrdt>                           secure_crdt_;
    };
} // namespace

TEST_F( SecureCrdtCandidateRaceTest, SynchronizedCandidatesCoexistDeduplicateAndBecomeStaleTogether )
{
    const auto                   candidate_a = SignedRecord( 0, 'a' );
    const auto                   candidate_b = SignedRecord( 0, 'b' );
    TwoPartyBarrier              barrier;
    outcome::result<CandidateId> result_a = SecureCrdt::Error::CANDIDATE_CONTEXT_MISMATCH;
    outcome::result<CandidateId> result_b = SecureCrdt::Error::CANDIDATE_CONTEXT_MISMATCH;

    std::thread submit_a(
        [&]
        {
            barrier.ArriveAndWait();
            result_a = secure_crdt_->SubmitCandidateApproval( candidate_a );
        } );
    std::thread submit_b(
        [&]
        {
            barrier.ArriveAndWait();
            result_b = secure_crdt_->SubmitCandidateApproval( candidate_b );
        } );
    submit_a.join();
    submit_b.join();

    ASSERT_TRUE( result_a.has_value() );
    ASSERT_TRUE( result_b.has_value() );
    EXPECT_FALSE( result_a.value() == result_b.value() );

    auto active = secure_crdt_->ListCandidates( "trusted-peer", PREDECESSOR );
    ASSERT_TRUE( active.has_value() );
    EXPECT_EQ( active.value().size(), 2U );

    EXPECT_EQ( secure_crdt_->SubmitCandidateApproval( candidate_a ).error(),
               SecureCrdt::Error::DUPLICATE_CANDIDATE_APPROVAL );
    auto approvals_a = secure_crdt_->ReadCandidateApprovals( result_a.value() );
    auto approvals_b = secure_crdt_->ReadCandidateApprovals( result_b.value() );
    ASSERT_TRUE( approvals_a.has_value() );
    ASSERT_TRUE( approvals_b.has_value() );
    EXPECT_EQ( approvals_a.value().size(), 1U );
    EXPECT_EQ( approvals_b.value().size(), 1U );

    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( SignedRecord( 1, 'a' ) ).has_value() );
    approvals_a = secure_crdt_->ReadCandidateApprovals( result_a.value() );
    approvals_b = secure_crdt_->ReadCandidateApprovals( result_b.value() );
    EXPECT_EQ( approvals_a.value().size(), 2U );
    EXPECT_EQ( approvals_b.value().size(), 1U );

    authorization_.next_version            = 8;
    authorization_.expected_previous_hash  = SUCCESSOR;
    authorization_.authorizing_policy_hash = SUCCESSOR;
    active                                 = secure_crdt_->ListCandidates( "trusted-peer", PREDECESSOR );
    ASSERT_TRUE( active.has_value() );
    EXPECT_TRUE( active.value().empty() );

    const auto audit = secure_crdt_->ListCandidates( "trusted-peer", PREDECESSOR, false );
    ASSERT_TRUE( audit.has_value() );
    EXPECT_EQ( audit.value().size(), 2U );
    EXPECT_EQ( secure_crdt_->SubmitCandidateApproval( SignedRecord( 1, 'b' ) ).error(),
               SecureCrdt::Error::CANDIDATE_CONTEXT_MISMATCH );
}
