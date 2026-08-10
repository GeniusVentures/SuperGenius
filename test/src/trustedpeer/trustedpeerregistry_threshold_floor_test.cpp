/**
 * @file       trustedpeerregistry_threshold_floor_test.cpp
 * @brief      D-07: proves TrustedPeerRegistry::New enforces the
 *             majority-floor quorum-threshold check (ceil(0.51*N)) at
 *             construction time -- a below-floor threshold fails to
 *             construct, an at-or-above-floor threshold succeeds, and the
 *             trivial 1-peer pre-genesis bootstrapper case is never rejected.
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

    class TrustedPeerRegistryThresholdFloorTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "trustedpeer_threshold_floor" );
            ASSERT_NE( node_, nullptr );

            secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( node_->db, "trustedpeer-floor-topic" );
            secure_crdt_->RegisterFilters();
        }

        void TearDown() override
        {
            secure_crdt_.reset();
            node_.reset();
        }

        std::unique_ptr<sgns::test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<sgns::securecrdt::SecureCrdt>               secure_crdt_;
    };
} // namespace

TEST_F( TrustedPeerRegistryThresholdFloorTest, BelowFloorThresholdFailsToConstruct )
{
    const std::vector<std::string> genesis_peers = {
        "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c9bca5ef065da131add4e5a2",
        "07b22cde0334a57625318c0662ae7571251642f9dea5ea8b7a2d7ceef2a63eb80d15a092ec410436948108ddce0c2a40ec06696b5b8ab4de154c9020accd3d91",
    };
    const auto artifact = sgns::test::trustedpeer::GenerateGenesisCeremonyArtifact(
        TrustedPeerListPayload( genesis_peers ).SerializeToBytes() );

    // ceil(0.51*2) = 2, so threshold=1 is below the floor.
    auto result = TrustedPeerRegistry::New( secure_crdt_, genesis_peers, artifact.bootstrapper_address,
                                            /*quorum_threshold=*/1 );
    EXPECT_TRUE( result.has_error() );
}

TEST_F( TrustedPeerRegistryThresholdFloorTest, AtFloorThresholdSucceeds )
{
    const std::vector<std::string> genesis_peers = {
        "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c9bca5ef065da131add4e5a2",
        "07b22cde0334a57625318c0662ae7571251642f9dea5ea8b7a2d7ceef2a63eb80d15a092ec410436948108ddce0c2a40ec06696b5b8ab4de154c9020accd3d91",
    };
    const auto artifact = sgns::test::trustedpeer::GenerateGenesisCeremonyArtifact(
        TrustedPeerListPayload( genesis_peers ).SerializeToBytes() );

    // ceil(0.51*2) = 2, so threshold=2 satisfies the floor.
    auto result = TrustedPeerRegistry::New( secure_crdt_, genesis_peers, artifact.bootstrapper_address,
                                            /*quorum_threshold=*/2 );
    ASSERT_TRUE( result.has_value() ) << result.error().message();
    result.value()->Unregister();
}

TEST_F( TrustedPeerRegistryThresholdFloorTest, TrivialSingleBootstrapperCaseNeverRejected )
{
    const std::vector<std::string> genesis_peers = {
        "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c9bca5ef065da131add4e5a2",
    };
    const auto artifact = sgns::test::trustedpeer::GenerateGenesisCeremonyArtifact(
        TrustedPeerListPayload( genesis_peers ).SerializeToBytes() );

    // ceil(0.51*1) = 1, so the trivial 1-peer bootstrapper case with
    // threshold=1 is satisfied as a no-op -- never rejected.
    auto result = TrustedPeerRegistry::New( secure_crdt_, genesis_peers, artifact.bootstrapper_address,
                                            /*quorum_threshold=*/1 );
    ASSERT_TRUE( result.has_value() ) << result.error().message();
    result.value()->Unregister();
}
