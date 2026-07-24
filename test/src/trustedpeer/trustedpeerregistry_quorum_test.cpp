/**
 * @file       trustedpeerregistry_quorum_test.cpp
 * @brief      TPR-02: proves N-of-M quorum-gated membership changes -- a
 *             sub-quorum signature count never mutates GetCurrentPeers(), and
 *             a quorum-met membership change replaces the whole peer list.
 *             Also proves a signature from a non-member address never counts
 *             toward quorum.
 *
 *             TPR-03 (manual, code-inspection-only gate, not automatable as a
 *             unit test): run
 *                 grep -rn "secp256k1\|VerifySignature\|VerifyPayloadSignature" src/trustedpeer/
 *             and confirm it returns zero matches -- proving no bespoke
 *             signature/quorum logic exists in src/trustedpeer/ (all such
 *             logic is delegated to SecureCrdt/multisig). Documented in
 *             10-VALIDATION.md's Manual-Only Verifications table; run before
 *             /gsd:verify-work.
 */
// genesis_ceremony_helper.hpp pulls in nil::crypto3 algebra headers that use
// local variable names (B0/B1) colliding with <sys/termios.h>'s B0/B1 baud
// macros if termios.h is transitively included first (e.g. via boost/gtest).
// Must be included before gtest/boost, mirroring GeniusAccount.cpp's own
// crypto3-headers-first include ordering (lines 1-4).
#include "genesis_ceremony_helper.hpp"

#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/GeniusAccount.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::trustedpeer;

    constexpr const char *PRIVATE_KEYS[] = {
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac2",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac3",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac4",
    };

    constexpr const char *NON_MEMBER_PRIVATE_KEY =
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac5";

    class TrustedPeerRegistryQuorumTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

            for ( const char *key : PRIVATE_KEYS )
            {
                auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), key, path_ );
                signers_.push_back( account );
                genesis_peers_.push_back( account->GetAddress() );
            }
            non_member_ = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), NON_MEMBER_PRIVATE_KEY, path_ );

            node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "trustedpeer_quorum" );
            ASSERT_NE( node_, nullptr );

            secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( node_->db, "trustedpeer-quorum-topic" );
            secure_crdt_->RegisterFilters();
        }

        void TearDown() override
        {
            if ( registry_ )
            {
                registry_->Unregister();
                registry_.reset();
            }
            secure_crdt_.reset();
            node_.reset();
            GeniusAccount::SetSecureStorageFactory( nullptr );
            boost::filesystem::remove_all( path_ );
        }

        /// @brief Bootstraps genesis (threshold 1, ephemeral bootstrapper) so
        ///        this test's registry has a confirmed genesis state before
        ///        exercising post-genesis membership-change quorum behavior.
        void ConfirmGenesis()
        {
            const auto genesis_payload = TrustedPeerListPayload( genesis_peers_ ).SerializeToBytes();
            const auto artifact        = sgns::test::trustedpeer::GenerateGenesisCeremonyArtifact( genesis_payload );

            auto new_result =
                TrustedPeerRegistry::New( secure_crdt_, genesis_peers_, artifact.bootstrapper_address, /*threshold=*/2 );
            ASSERT_FALSE( new_result.has_error() ) << new_result.error().message();
            registry_ = new_result.value();

            auto seed_result = registry_->SeedGenesis( genesis_peers_, artifact.signature );
            ASSERT_FALSE( seed_result.has_error() ) << seed_result.error().message();

            auto confirm_result = registry_->TryConfirm();
            ASSERT_FALSE( confirm_result.has_error() ) << confirm_result.error().message();
            ASSERT_TRUE( confirm_result.value() );
            ASSERT_TRUE( registry_->IsGenesisConfirmed() );
        }

        boost::filesystem::path                     path_;
        std::vector<std::shared_ptr<GeniusAccount>> signers_;
        std::shared_ptr<GeniusAccount>               non_member_;
        std::vector<std::string>                    genesis_peers_;
        std::unique_ptr<sgns::test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<sgns::securecrdt::SecureCrdt>               secure_crdt_;
        std::shared_ptr<TrustedPeerRegistry>                        registry_;
    };
} // namespace

TEST_F( TrustedPeerRegistryQuorumTest, SubQuorumSignatureNeverMutatesPeersThenQuorumMetReplacesWholeList )
{
    ConfirmGenesis();

    const std::vector<std::string> new_peers = { genesis_peers_[0], non_member_->GetAddress() };
    const auto                     new_payload = TrustedPeerListPayload( new_peers ).SerializeToBytes();

    auto propose_result = registry_->ProposeMembershipChange( new_peers );
    ASSERT_FALSE( propose_result.has_error() ) << propose_result.error().message();

    // 1st signature from a CURRENT genesis signer -- sub-quorum (threshold 2).
    auto sig0 = signers_[0]->Sign( new_payload );
    auto sign0_result = registry_->SignMembershipChange( signers_[0]->GetAddress(), sig0 );
    ASSERT_FALSE( sign0_result.has_error() ) << sign0_result.error().message();

    auto confirm_after_one = registry_->TryConfirm();
    ASSERT_FALSE( confirm_after_one.has_error() ) << confirm_after_one.error().message();
    EXPECT_FALSE( confirm_after_one.value() ) << "1 signature < threshold 2 must never report quorum met";
    EXPECT_EQ( registry_->GetCurrentPeers(), genesis_peers_ ) << "sub-quorum must leave old genesis list untouched";

    // 2nd signature from a DIFFERENT current genesis signer -- quorum met.
    auto sig1 = signers_[1]->Sign( new_payload );
    auto sign1_result = registry_->SignMembershipChange( signers_[1]->GetAddress(), sig1 );
    ASSERT_FALSE( sign1_result.has_error() ) << sign1_result.error().message();

    auto confirm_after_two = registry_->TryConfirm();
    ASSERT_FALSE( confirm_after_two.has_error() ) << confirm_after_two.error().message();
    EXPECT_TRUE( confirm_after_two.value() ) << "2 signatures >= threshold 2 must report quorum met";
    EXPECT_EQ( registry_->GetCurrentPeers(), new_peers ) << "quorum met must replace the whole peer list";
}

TEST_F( TrustedPeerRegistryQuorumTest, SignatureFromNonMemberAddressNeverCountsTowardQuorum )
{
    ConfirmGenesis();

    const std::vector<std::string> new_peers   = { genesis_peers_[0], genesis_peers_[1] };
    const auto                     new_payload = TrustedPeerListPayload( new_peers ).SerializeToBytes();

    auto propose_result = registry_->ProposeMembershipChange( new_peers );
    ASSERT_FALSE( propose_result.has_error() ) << propose_result.error().message();

    // Cryptographically VALID signature, but from an address NOT in the current genesis set.
    auto non_member_sig = non_member_->Sign( new_payload );
    auto sign_result     = registry_->SignMembershipChange( non_member_->GetAddress(), non_member_sig );
    ASSERT_FALSE( sign_result.has_error() ) << sign_result.error().message();

    auto confirm_after_non_member = registry_->TryConfirm();
    ASSERT_FALSE( confirm_after_non_member.has_error() ) << confirm_after_non_member.error().message();
    EXPECT_FALSE( confirm_after_non_member.value() )
        << "a valid signature from a non-member address must never contribute toward quorum";
    EXPECT_EQ( registry_->GetCurrentPeers(), genesis_peers_ );

    // A 2nd genuine genesis-signer signature is still required to actually meet quorum.
    auto sig0 = signers_[0]->Sign( new_payload );
    auto sign0_result = registry_->SignMembershipChange( signers_[0]->GetAddress(), sig0 );
    ASSERT_FALSE( sign0_result.has_error() ) << sign0_result.error().message();

    auto confirm_after_one_member = registry_->TryConfirm();
    ASSERT_FALSE( confirm_after_one_member.has_error() ) << confirm_after_one_member.error().message();
    EXPECT_FALSE( confirm_after_one_member.value() )
        << "only 1 counted (member) signature must remain below threshold 2 (non-member signature never counted)";
    EXPECT_EQ( registry_->GetCurrentPeers(), genesis_peers_ );
}
