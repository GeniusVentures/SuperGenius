/**
 * @file       burnconfig_test.cpp
 * @brief      BURN-01: proves BurnConfig's genesis auto-seed, cache-refresh
 *             via quorum re-derivation, pre-quorum default fallback
 *             (BURN-03), and majority-floor threshold rejection (D-07
 *             regression), built entirely on top of SecureCrdt/
 *             TrustedPeerRegistry against a genuine single-node GlobalDB
 *             fixture (mirrors trustedpeerregistry_genesis_test.cpp).
 */
// genesis_ceremony_helper-adjacent crypto3 headers use local variable names
// (B0/B1) colliding with <sys/termios.h>'s B0/B1 baud macros if termios.h is
// transitively included first (e.g. via boost/gtest). GeniusAccount.hpp must
// be included before gtest/boost, mirroring GeniusAccount.cpp's own
// crypto3-headers-first include ordering.
#include "account/GeniusAccount.hpp"

#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/BurnConfig.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::account;
    using namespace sgns::trustedpeer;

    constexpr const char *SELF_PRIVATE_KEY = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac2";
    constexpr const char *OTHER_PRIVATE_KEY = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac3";
    constexpr const char *OBSERVER_PRIVATE_KEY = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac4";

    class BurnConfigTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

            self_account_     = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), SELF_PRIVATE_KEY, path_ );
            other_account_    = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), OTHER_PRIVATE_KEY, path_ );
            observer_account_ =
                GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), OBSERVER_PRIVATE_KEY, path_ );

            node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "burnconfig" );
            ASSERT_NE( node_, nullptr );

            secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( node_->db, "burnconfig-topic" );
            secure_crdt_->RegisterFilters();
        }

        void TearDown() override
        {
            if ( burn_config_ )
            {
                burn_config_->Unregister();
                burn_config_.reset();
            }
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

        boost::filesystem::path                                     path_;
        std::shared_ptr<GeniusAccount>                               self_account_;
        std::shared_ptr<GeniusAccount>                               other_account_;
        std::shared_ptr<GeniusAccount>                               observer_account_;
        std::unique_ptr<sgns::test::securecrdt::SecureCrdtTestNode>  node_;
        std::shared_ptr<sgns::securecrdt::SecureCrdt>                secure_crdt_;
        std::shared_ptr<TrustedPeerRegistry>                         registry_;
        std::shared_ptr<BurnConfig>                                  burn_config_;
    };
} // namespace

TEST_F( BurnConfigTest, BurnconfigGenesisAutoSeed )
{
    const std::vector<std::string> genesis_peers = { self_account_->GetAddress() };

    auto registry_result = TrustedPeerRegistry::New( secure_crdt_, genesis_peers, self_account_->GetAddress(),
                                                      /*quorum_threshold=*/1 );
    ASSERT_TRUE( registry_result.has_value() ) << registry_result.error().message();
    registry_ = registry_result.value();

    auto burn_config_result =
        BurnConfig::New( secure_crdt_, node_->db, registry_, /*quorum_threshold=*/1, self_account_ );
    ASSERT_TRUE( burn_config_result.has_value() ) << burn_config_result.error().message();
    burn_config_ = burn_config_result.value();

    EXPECT_EQ( burn_config_->GetCachedBasisPoints(), BurnConfig::GENESIS_DEFAULT_BASIS_POINTS );
}

TEST_F( BurnConfigTest, BurnconfigCacheRefresh )
{
    // BurnConfig is constructed with observer_account_, which is NOT one of
    // the genesis peers -- this deliberately keeps TrySeedGenesisIfEligible
    // from firing its own auto-seed ProposeValue+AddSignature(self, 100)
    // during construction, which would otherwise race/collide with this
    // test's own manual propose(250)+sign sequence on the same base_key.
    const std::vector<std::string> genesis_peers = { self_account_->GetAddress(), other_account_->GetAddress() };

    auto registry_result = TrustedPeerRegistry::New( secure_crdt_, genesis_peers, self_account_->GetAddress(),
                                                      /*quorum_threshold=*/2 );
    ASSERT_TRUE( registry_result.has_value() ) << registry_result.error().message();
    registry_ = registry_result.value();

    auto burn_config_result =
        BurnConfig::New( secure_crdt_, node_->db, registry_, /*quorum_threshold=*/2, observer_account_ );
    ASSERT_TRUE( burn_config_result.has_value() ) << burn_config_result.error().message();
    burn_config_ = burn_config_result.value();

    uint64_t refreshed_value  = 0;
    int      refresh_count    = 0;
    burn_config_->RegisterRefreshCallback(
        [&refreshed_value, &refresh_count]( uint64_t new_value )
        {
            refreshed_value = new_value;
            ++refresh_count;
        } );

    const BurnConfigPayload new_payload( 250 );
    const auto              serialized = new_payload.SerializeToBytes();

    auto propose_result = secure_crdt_->ProposeValue( sgns::crdt::HierarchicalKey( "burn-config" ), serialized );
    ASSERT_FALSE( propose_result.has_error() ) << propose_result.error().message();

    const auto signature_bytes = self_account_->Sign( serialized );
    auto       sign_result     = secure_crdt_->AddSignature( sgns::crdt::HierarchicalKey( "burn-config" ),
                                                             self_account_->GetAddress(), signature_bytes );
    ASSERT_FALSE( sign_result.has_error() ) << sign_result.error().message();

    EXPECT_EQ( refresh_count, 0 ) << "quorum (2) not yet met with only 1/2 signatures -- refresh must not fire early";

    const auto other_signature_bytes = other_account_->Sign( serialized );
    auto       other_sign_result     = secure_crdt_->AddSignature(
        sgns::crdt::HierarchicalKey( "burn-config" ), other_account_->GetAddress(), other_signature_bytes );
    ASSERT_FALSE( other_sign_result.has_error() ) << other_sign_result.error().message();

    EXPECT_EQ( refresh_count, 1 ) << "the refresh callback must fire exactly once, once quorum (2/2) is met";
    EXPECT_EQ( refreshed_value, 250u );
    EXPECT_EQ( burn_config_->GetCachedBasisPoints(), 250u );
}

TEST_F( BurnConfigTest, BurnconfigGenesisDefaultBeforeQuorum )
{
    // A genesis peer set that does NOT include self_account_ -- self is
    // never eligible to auto-seed, and no other signer proposes/signs
    // anything, so the cache stays at the BURN-03 pre-quorum default.
    const std::vector<std::string> genesis_peers = { other_account_->GetAddress() };

    auto registry_result = TrustedPeerRegistry::New( secure_crdt_, genesis_peers, other_account_->GetAddress(),
                                                      /*quorum_threshold=*/1 );
    ASSERT_TRUE( registry_result.has_value() ) << registry_result.error().message();
    registry_ = registry_result.value();

    auto burn_config_result =
        BurnConfig::New( secure_crdt_, node_->db, registry_, /*quorum_threshold=*/1, self_account_ );
    ASSERT_TRUE( burn_config_result.has_value() ) << burn_config_result.error().message();
    burn_config_ = burn_config_result.value();

    EXPECT_EQ( burn_config_->GetCachedBasisPoints(), BurnConfig::GENESIS_DEFAULT_BASIS_POINTS );
}

TEST_F( BurnConfigTest, BurnconfigThresholdFloorRejection )
{
    const std::vector<std::string> genesis_peers = { self_account_->GetAddress(), other_account_->GetAddress() };

    // ceil(0.51*2) = 2, so registry threshold=2 is required for the registry
    // itself; the registry construction succeeds, but BurnConfig::New with a
    // below-floor threshold=1 for the same 2-peer signer set must fail.
    auto registry_result = TrustedPeerRegistry::New( secure_crdt_, genesis_peers, self_account_->GetAddress(),
                                                      /*quorum_threshold=*/2 );
    ASSERT_TRUE( registry_result.has_value() ) << registry_result.error().message();
    registry_ = registry_result.value();

    auto burn_config_result =
        BurnConfig::New( secure_crdt_, node_->db, registry_, /*quorum_threshold=*/1, self_account_ );
    EXPECT_TRUE( burn_config_result.has_error() );
}
