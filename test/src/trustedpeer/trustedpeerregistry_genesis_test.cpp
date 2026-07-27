/**
 * @file       trustedpeerregistry_genesis_test.cpp
 * @brief      TPR-01: proves TrustedPeerRegistry's genesis-seeding behavior --
 *             local visibility at construction, real end-to-end confirmation
 *             via a genuine SecureCrdt-backed single-node fixture, and payload
 *             binding of the ephemeral genesis signature.
 */
#include <gtest/gtest.h>

#include "genesis_ceremony_helper.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::trustedpeer;

    class TrustedPeerRegistryGenesisTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "trustedpeer_genesis" );
            ASSERT_NE( node_, nullptr );

            secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( node_->db, "trustedpeer-topic" );
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
        }

        std::unique_ptr<sgns::test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<sgns::securecrdt::SecureCrdt>               secure_crdt_;
        std::shared_ptr<TrustedPeerRegistry>                        registry_;
    };
} // namespace

TEST_F( TrustedPeerRegistryGenesisTest, GenesisPeersVisibleLocallyBeforeConfirmation )
{
    const std::vector<std::string> genesis_peers = {
        "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c9bca5ef065da131add4e5a2",
        "07b22cde0334a57625318c0662ae7571251642f9dea5ea8b7a2d7ceef2a63eb80d15a092ec410436948108ddce0c2a40ec06696b5b8ab4de154c9020accd3d91",
    };
    const auto artifact =
        sgns::test::trustedpeer::GenerateGenesisCeremonyArtifact( TrustedPeerListPayload( genesis_peers ).SerializeToBytes() );

    auto new_result = TrustedPeerRegistry::New( secure_crdt_, genesis_peers, artifact.bootstrapper_address, /*threshold=*/2 );
    ASSERT_TRUE( new_result.has_value() ) << new_result.error().message();
    registry_ = new_result.value();

    EXPECT_EQ( registry_->GetCurrentPeers(), genesis_peers );
    EXPECT_FALSE( registry_->IsGenesisConfirmed() );
}

TEST_F( TrustedPeerRegistryGenesisTest, ValidGenesisSeedConfirmsEndToEnd )
{
    const std::vector<std::string> genesis_peers = {
        "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c9bca5ef065da131add4e5a2",
        "07b22cde0334a57625318c0662ae7571251642f9dea5ea8b7a2d7ceef2a63eb80d15a092ec410436948108ddce0c2a40ec06696b5b8ab4de154c9020accd3d91",
    };
    const auto payload  = TrustedPeerListPayload( genesis_peers ).SerializeToBytes();
    const auto artifact = sgns::test::trustedpeer::GenerateGenesisCeremonyArtifact( payload );

    auto new_result = TrustedPeerRegistry::New( secure_crdt_, genesis_peers, artifact.bootstrapper_address, /*threshold=*/2 );
    ASSERT_TRUE( new_result.has_value() ) << new_result.error().message();
    registry_ = new_result.value();

    auto seed_result = registry_->SeedGenesis( genesis_peers, artifact.signature );
    ASSERT_FALSE( seed_result.has_error() ) << seed_result.error().message();

    auto confirm_result = registry_->TryConfirm();
    ASSERT_FALSE( confirm_result.has_error() ) << confirm_result.error().message();
    EXPECT_TRUE( confirm_result.value() );
    EXPECT_TRUE( registry_->IsGenesisConfirmed() );
    EXPECT_EQ( registry_->GetCurrentPeers(), genesis_peers );
}

TEST_F( TrustedPeerRegistryGenesisTest, SignatureOverMismatchedPayloadNeverConfirms )
{
    const std::vector<std::string> genesis_peers = {
        "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c9bca5ef065da131add4e5a2",
        "07b22cde0334a57625318c0662ae7571251642f9dea5ea8b7a2d7ceef2a63eb80d15a092ec410436948108ddce0c2a40ec06696b5b8ab4de154c9020accd3d91",
    };
    const std::vector<std::string> different_peers = {
        "06a11bcf9223a46514207b0551ed6460140531e8ec94d97a6a1c6bddd1a52da79e04980db3009325837f97ccbd1b1e3fdf05585a4a79ab3d043b7f19bbbc2c80",
    };
    // Ephemeral signature is produced over a DIFFERENT payload than what is proposed.
    const auto mismatched_payload = TrustedPeerListPayload( different_peers ).SerializeToBytes();
    const auto artifact           = sgns::test::trustedpeer::GenerateGenesisCeremonyArtifact( mismatched_payload );

    auto new_result = TrustedPeerRegistry::New( secure_crdt_, genesis_peers, artifact.bootstrapper_address, /*threshold=*/2 );
    ASSERT_TRUE( new_result.has_value() ) << new_result.error().message();
    registry_ = new_result.value();

    auto seed_result = registry_->SeedGenesis( genesis_peers, artifact.signature );
    // SeedGenesis proposes genesis_peers, then attempts to add a signature that was
    // produced over a different payload -- SecureCrdt::AddSignature rejects it locally.
    EXPECT_TRUE( seed_result.has_error() );

    auto confirm_result = registry_->TryConfirm();
    ASSERT_FALSE( confirm_result.has_error() ) << confirm_result.error().message();
    EXPECT_FALSE( confirm_result.value() );
    EXPECT_FALSE( registry_->IsGenesisConfirmed() );
    EXPECT_EQ( registry_->GetCurrentPeers(), genesis_peers ) << "cache must remain the constructor-seeded value";
}
