#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/GeniusAccount.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/SecureCrdtCandidate.hpp"
#include "securecrdt_test_node.hpp"
#include "testutil/wait_condition.hpp"

namespace
{
    using namespace sgns::securecrdt;

    const std::string HASH_A( 64, 'a' );
    const std::string HASH_B( 64, 'b' );
    const std::string SIGNER_A( 128, 'c' );
    const std::string SIGNER_B( 128, 'd' );

    CandidateCore MakeCore()
    {
        CandidateCore core;
        core.domain                  = "trusted-peer";
        core.network_id              = 42;
        core.kind                    = CandidateKind::TrustPolicy;
        core.version                 = 7;
        core.expected_previous_hash  = HASH_A;
        core.authorizing_policy_hash = HASH_B;
        core.payload                 = { 'p', 'o', 'l', 'i', 'c', 'y' };
        return core;
    }

    CandidateApprovalRecord MakeApproval( CandidateCore core = MakeCore(), std::string signer = SIGNER_A )
    {
        return CandidateApprovalRecord{ CandidateApprovalRecord::ENCODING_VERSION,
                                        std::move( core ),
                                        std::move( signer ),
                                        { 0x01, 0x02, 0x03 } };
    }
} // namespace

class SecureCrdtCandidateAuthorizationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sgns::GeniusAccount::SetSecureStorageFactory(
            []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
            { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
        path_                        = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        constexpr const char *keys[] = {
            "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eab0",
            "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eab1",
            "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eab2",
        };
        for ( const auto *key : keys )
        {
            signers_.push_back(
                sgns::GeniusAccount::NewFromPrivateKey( sgns::TokenID::FromBytes( { 0 } ), key, path_ ) );
        }
        node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "securecrdt_candidate" );
        ASSERT_NE( node_, nullptr );
        secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( node_->db, "securecrdt_test_topic" );

        authorization_.network_id              = 42;
        authorization_.kind                    = CandidateKind::TrustPolicy;
        authorization_.next_version            = 7;
        authorization_.expected_previous_hash  = HASH_A;
        authorization_.authorizing_policy_hash = HASH_B;
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
        if ( secure_crdt_ )
        {
            secure_crdt_->UnregisterCandidateCallbackIf( "trusted-peer", &owner_token_ );
            secure_crdt_->Registry().UnregisterCandidateDomainIf( "trusted-peer", &owner_token_ );
            secure_crdt_.reset();
        }
        if ( node_ )
        {
            node_.reset();
        }
        sgns::GeniusAccount::SetSecureStorageFactory( nullptr );
        if ( !path_.empty() )
        {
            boost::filesystem::remove_all( path_ );
        }
    }

    CandidateApprovalRecord SignedRecord( size_t signer_index, std::vector<uint8_t> payload = { 'p' } )
    {
        auto core        = MakeCore();
        core.payload     = std::move( payload );
        const auto bytes = core.CanonicalBytes();
        EXPECT_TRUE( bytes.has_value() );
        return CandidateApprovalRecord{ CandidateApprovalRecord::ENCODING_VERSION,
                                        core,
                                        signers_[signer_index]->GetAddress(),
                                        signers_[signer_index]->Sign( *bytes ) };
    }

    boost::filesystem::path                                     path_;
    int                                                         owner_token_ = 0;
    CandidateAuthorizationSnapshot                              authorization_;
    std::vector<std::shared_ptr<sgns::GeniusAccount>>           signers_;
    std::unique_ptr<sgns::test::securecrdt::SecureCrdtTestNode> node_;
    std::shared_ptr<sgns::securecrdt::SecureCrdt>               secure_crdt_;
};

TEST_F( SecureCrdtCandidateAuthorizationTest, LocalRemoteAuthorizationRetainsOnlyCurrentPeerRecords )
{
    auto       valid    = SignedRecord( 0 );
    const auto accepted = secure_crdt_->SubmitCandidateApproval( valid );
    ASSERT_TRUE( accepted.has_value() );
    const CandidateKey accepted_key{ accepted.value(), valid.signer };
    EXPECT_TRUE( node_->db->Get( accepted_key.ToHierarchicalKey() ).has_value() );

    auto       outsider    = SignedRecord( 2, { 'o' } );
    const auto outsider_id = CandidateId::FromCore( outsider.core );
    ASSERT_TRUE( outsider_id.has_value() );
    EXPECT_EQ( secure_crdt_->SubmitCandidateApproval( outsider ).error(),
               SecureCrdt::Error::UNAUTHORIZED_CANDIDATE_SIGNER );
    EXPECT_TRUE( node_->db->Get( CandidateKey{ *outsider_id, outsider.signer }.ToHierarchicalKey() ).has_error() );

    auto stale                         = SignedRecord( 0, { 's' } );
    stale.core.authorizing_policy_hash = HASH_A;
    const auto stale_bytes             = stale.core.CanonicalBytes();
    ASSERT_TRUE( stale_bytes.has_value() );
    stale.signature = signers_[0]->Sign( *stale_bytes );
    EXPECT_EQ( secure_crdt_->SubmitCandidateApproval( stale ).error(), SecureCrdt::Error::CANDIDATE_CONTEXT_MISMATCH );
}

TEST_F( SecureCrdtCandidateAuthorizationTest, CallbackSurfacesAcceptedRecordWithoutCreatingApproval )
{
    std::atomic<size_t> callbacks{ 0 };
    ASSERT_TRUE( secure_crdt_->RegisterCandidateCallback(
        "trusted-peer",
        [&]( const CandidateId &, const CandidateApprovalRecord & ) { callbacks.fetch_add( 1 ); },
        &owner_token_ ) );

    auto record = SignedRecord( 0 );
    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( record ).has_value() );
    sgns::test::assertWaitForCondition( [&] { return callbacks.load() == 1; },
                                        std::chrono::seconds( 5 ),
                                        "candidate callback did not run" );
    const auto id = CandidateId::FromCore( record.core );
    ASSERT_TRUE( id.has_value() );
    const auto approvals = secure_crdt_->ReadCandidateApprovals( *id );
    ASSERT_TRUE( approvals.has_value() );
    EXPECT_EQ( approvals.value().size(), 1U );
}

TEST_F( SecureCrdtCandidateAuthorizationTest, LimitKeepsExistingCandidateApprovalsAdmissibleAtCreationCap )
{
    CandidateId first;
    for ( size_t index = 0; index < CandidateLimits::MAX_ACTIVE_CANDIDATES_PER_PREDECESSOR; ++index )
    {
        auto       record    = SignedRecord( 0, { static_cast<uint8_t>( index ) } );
        const auto submitted = secure_crdt_->SubmitCandidateApproval( record );
        ASSERT_TRUE( submitted.has_value() ) << index;
        if ( index == 0 )
        {
            first = submitted.value();
        }
    }

    EXPECT_EQ( secure_crdt_->SubmitCandidateApproval( SignedRecord( 0, { 0xff } ) ).error(),
               SecureCrdt::Error::CANDIDATE_LIMIT_EXCEEDED );

    auto second_approval = SignedRecord( 1, { 0 } );
    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( second_approval ).has_value() );
    const auto approvals = secure_crdt_->ReadCandidateApprovals( first );
    ASSERT_TRUE( approvals.has_value() );
    EXPECT_EQ( approvals.value().size(), 2U );
    EXPECT_EQ( secure_crdt_->SubmitCandidateApproval( second_approval ).error(),
               SecureCrdt::Error::DUPLICATE_CANDIDATE_APPROVAL );
}

TEST( SecureCrdtCandidateCodecTest, CanonicalCoreRoundTripsAndBindsEveryContextField )
{
    const auto core  = MakeCore();
    const auto bytes = core.CanonicalBytes();
    ASSERT_TRUE( bytes.has_value() );
    EXPECT_EQ( CandidateCore::DecodeCanonical( *bytes ), core );

    const auto hash = core.Hash();
    ASSERT_TRUE( hash.has_value() );
    EXPECT_EQ( hash->size(), 64U );

    auto mutated = core;
    mutated.network_id++;
    EXPECT_NE( mutated.Hash(), hash );
    mutated      = core;
    mutated.kind = CandidateKind::BurnConfig;
    EXPECT_NE( mutated.Hash(), hash );
    mutated                        = core;
    mutated.expected_previous_hash = HASH_B;
    EXPECT_NE( mutated.Hash(), hash );
    mutated                         = core;
    mutated.authorizing_policy_hash = HASH_A;
    EXPECT_NE( mutated.Hash(), hash );
    mutated = core;
    mutated.payload.push_back( 0xff );
    EXPECT_NE( mutated.Hash(), hash );
}

TEST( SecureCrdtCandidateCodecTest, RejectsUnknownVersionKindTrailingBytesAndNonCanonicalContext )
{
    const auto bytes = MakeCore().CanonicalBytes();
    ASSERT_TRUE( bytes.has_value() );

    auto unknown_encoding                                 = *bytes;
    unknown_encoding[CandidateCore::RECORD_DOMAIN.size()] = 2;
    EXPECT_FALSE( CandidateCore::DecodeCanonical( unknown_encoding ).has_value() );

    auto         unknown_kind = *bytes;
    const size_t kind_offset  = CandidateCore::RECORD_DOMAIN.size() + 1 + 4 + MakeCore().domain.size() + 2;
    ASSERT_LT( kind_offset, unknown_kind.size() );
    unknown_kind[kind_offset] = 0xff;
    EXPECT_FALSE( CandidateCore::DecodeCanonical( unknown_kind ).has_value() );

    auto trailing = *bytes;
    trailing.push_back( 0 );
    EXPECT_FALSE( CandidateCore::DecodeCanonical( trailing ).has_value() );

    auto malformed   = MakeCore();
    malformed.domain = "trusted-peer/candidate";
    EXPECT_FALSE( malformed.CanonicalBytes().has_value() );
    malformed                           = MakeCore();
    malformed.expected_previous_hash[0] = 'A';
    EXPECT_FALSE( malformed.CanonicalBytes().has_value() );
}

TEST( SecureCrdtCandidateKeyTest, ExactPathRoundTripsAndRejectsPathConfusion )
{
    const auto id = CandidateId::FromCore( MakeCore() );
    ASSERT_TRUE( id.has_value() );
    const CandidateKey key{ *id, SIGNER_A };
    const auto         canonical = key.ToHierarchicalKey();
    EXPECT_EQ( canonical.GetKey(), "/trusted-peer/candidate/v7/" + id->content_hash + "/approval/" + SIGNER_A );
    const auto parsed = CandidateKey::Parse( canonical );
    ASSERT_TRUE( parsed.has_value() );
    EXPECT_EQ( parsed->id, *id );
    EXPECT_EQ( parsed->signer, SIGNER_A );

    EXPECT_FALSE( CandidateKey::Parse(
                      sgns::crdt::HierarchicalKey( "/trusted-peer/candidate/v7/" + id->content_hash + "/approval" ) )
                      .has_value() );
    EXPECT_FALSE( CandidateKey::Parse( sgns::crdt::HierarchicalKey( "/trusted-peer/candidate/v7/" + id->content_hash +
                                                                    "/approval/" + SIGNER_A + "/extra" ) )
                      .has_value() );
    EXPECT_FALSE( CandidateKey::Parse( sgns::crdt::HierarchicalKey( "/trusted-peer/candidate/7/" + id->content_hash +
                                                                    "/approval/" + SIGNER_A ) )
                      .has_value() );
    EXPECT_FALSE( CandidateKey::Parse( sgns::crdt::HierarchicalKey( "/trusted-peer/candidate/v7/" +
                                                                    std::string( 64, 'A' ) + "/approval/" + SIGNER_A ) )
                      .has_value() );
    EXPECT_FALSE( CandidateKey::Parse( sgns::crdt::HierarchicalKey( "/trusted-peer/candidate/v7/" + id->content_hash +
                                                                    "/approval/" + std::string( 128, 'C' ) ) )
                      .has_value() );
}

TEST( SecureCrdtCandidateKeyTest, ApprovalRecordRejectsCandidateAndSignerMismatchBeforeVerification )
{
    const auto record = MakeApproval();
    const auto bytes  = record.CanonicalBytes();
    const auto id     = CandidateId::FromCore( record.core );
    ASSERT_TRUE( bytes.has_value() );
    ASSERT_TRUE( id.has_value() );

    EXPECT_TRUE( CandidateApprovalRecord::DecodeCanonical( *bytes, CandidateKey{ *id, SIGNER_A } ).has_value() );

    auto candidate_b = record.core;
    candidate_b.payload.push_back( 'b' );
    const auto id_b = CandidateId::FromCore( candidate_b );
    ASSERT_TRUE( id_b.has_value() );
    EXPECT_FALSE( CandidateApprovalRecord::DecodeCanonical( *bytes, CandidateKey{ *id_b, SIGNER_A } ).has_value() );
    EXPECT_FALSE( CandidateApprovalRecord::DecodeCanonical( *bytes, CandidateKey{ *id, SIGNER_B } ).has_value() );
}

TEST( SecureCrdtCandidateBoundsTest, AcceptsExactlyMaximumCandidateBytesAndRejectsOneMore )
{
    auto core = MakeCore();
    core.payload.clear();
    const auto empty = core.CanonicalBytes();
    ASSERT_TRUE( empty.has_value() );
    ASSERT_LT( empty->size(), CandidateLimits::MAX_CANDIDATE_BYTES );

    core.payload.resize( CandidateLimits::MAX_CANDIDATE_BYTES - empty->size() );
    const auto exact = core.CanonicalBytes();
    ASSERT_TRUE( exact.has_value() );
    ASSERT_EQ( exact->size(), CandidateLimits::MAX_CANDIDATE_BYTES );
    EXPECT_TRUE( CandidateCore::DecodeCanonical( *exact ).has_value() );

    core.payload.push_back( 0 );
    EXPECT_FALSE( core.CanonicalBytes().has_value() );
    auto oversized = *exact;
    oversized.push_back( 0 );
    EXPECT_FALSE( CandidateCore::DecodeCanonical( oversized ).has_value() );
}

TEST( SecureCrdtCandidateBoundsTest, CandidateAndApprovalCountCapsAreInclusive )
{
    EXPECT_TRUE( CandidateLimits::CandidateCountAllowed( 31 ) );
    EXPECT_TRUE( CandidateLimits::CandidateCountAllowed( 32 ) );
    EXPECT_FALSE( CandidateLimits::CandidateCountAllowed( 33 ) );

    EXPECT_TRUE( CandidateLimits::ApprovalCountAllowed( 255 ) );
    EXPECT_TRUE( CandidateLimits::ApprovalCountAllowed( 256 ) );
    EXPECT_FALSE( CandidateLimits::ApprovalCountAllowed( 257 ) );
}

TEST( SecureCrdtCandidateBoundsTest, ApprovalByteAccountingIsOverflowSafeAt64MiBBoundary )
{
    constexpr size_t cap = CandidateLimits::MAX_ACTIVE_APPROVAL_BYTES_PER_PREDECESSOR;
    EXPECT_TRUE( CandidateLimits::ApprovalBytesAllowed( cap - 1, 1 ) );
    EXPECT_TRUE( CandidateLimits::ApprovalBytesAllowed( cap, 0 ) );
    EXPECT_FALSE( CandidateLimits::ApprovalBytesAllowed( cap, 1 ) );
    EXPECT_FALSE( CandidateLimits::ApprovalBytesAllowed( std::numeric_limits<size_t>::max(), 1 ) );
}
