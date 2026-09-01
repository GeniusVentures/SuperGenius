/**
 * @file       peer_registry_test.cpp
 * @brief      D-04/T-15-04: proves the explicit per-key PeerRegistry
 *             association - TrustedPeerRegistry self-associates its policy
 *             entry, base and sig-child keys resolve to the same authority,
 *             MakeRegistrySignerSetSource adapts the registry, and Unregister
 *             removes the association.
 */
#include <gtest/gtest.h>

#include <vector>

#include "peerregistry/PeerRegistry.hpp"
#include "securecrdt/ISignedCRDTData.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/SecureCrdtRegistry.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::trustedpeer;

    class TestSignedData : public sgns::securecrdt::ISignedCRDTData
    {
    public:
        std::vector<uint8_t> SerializeToBytes() const override
        {
            return {};
        }
        bool DeserializeFromBytes( const std::vector<uint8_t> & /*bytes*/ ) override
        {
            return true;
        }
        bool Verify( const std::vector<uint8_t> & /*payload*/ ) const override
        {
            return true;
        }
        void Apply() override
        {
        }
    };

    const std::vector<std::string> kGenesisPeers = {
        "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c9bca5ef065da131add4e5a2",
        "07b22cde0334a57625318c0662ae7571251642f9dea5ea8b7a2d7ceef2a63eb80d15a092ec410436948108ddce0c2a40ec06696b5b8ab4de154c9020accd3d91",
    };
    const std::string kBootstrapperAddress =
        "06a11bcf9223a46514207b0551ed6460140531e8ec94d97a6a1c6bddd1a52da79e04980db3009325837f97ccbd1b1e3fdf05585a4a79ab3d043b7f19bbbc2c80";
    // HierarchicalKey normalizes "trusted-peer-registry" to a leading-slash
    // form; SecureCrdt always resolves with base_key.GetKey(), so the
    // registered pattern is "/trusted-peer-registry".
    const std::string kRegistryBaseKey = "/trusted-peer-registry";

    class PeerRegistryAssociationTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "peerregistry" );
            ASSERT_NE( node_, nullptr );

            secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( node_->db, "peerregistry-topic" );
            secure_crdt_->RegisterFilters();
        }

        void TearDown() override
        {
            if ( registry_ )
            {
                registry_->Unregister();
                registry_.reset();
            }
            secure_crdt_->Registry().UnregisterIf( kHandmadeKey, &handmade_token_ );
            secure_crdt_.reset();
            node_.reset();
        }

        std::unique_ptr<sgns::test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<sgns::securecrdt::SecureCrdt>               secure_crdt_;
        std::shared_ptr<TrustedPeerRegistry>                        registry_;
        static constexpr const char                                *kHandmadeKey = "peerregistry-handmade-key";
        int                                                         handmade_token_ = 0;
    };

    TEST_F( PeerRegistryAssociationTest, RegisteredEntryCarriesRegistryAuthority )
    {
        auto new_result = TrustedPeerRegistry::New( secure_crdt_, kGenesisPeers, kBootstrapperAddress, /*threshold=*/2 );
        ASSERT_TRUE( new_result.has_value() ) << new_result.error().message();
        registry_ = new_result.value();

        const auto resolved = secure_crdt_->Registry().Resolve( kRegistryBaseKey );
        ASSERT_TRUE( resolved.has_value() );
        EXPECT_TRUE( resolved->peer_registry != nullptr ) << "policy entry must record its PeerRegistry authority";

        // The registry association and the TPR instance are the same authority.
        EXPECT_EQ( resolved->peer_registry.get(), std::static_pointer_cast<sgns::peerregistry::PeerRegistry>( registry_ ).get() );

        // The entry's source resolves the genesis (pre-confirmation) cached set:
        // the sole bootstrapper at threshold 1.
        ASSERT_TRUE( resolved->signer_set_source );
        const auto snapshot = resolved->signer_set_source( kRegistryBaseKey );
        ASSERT_FALSE( snapshot.has_error() ) << snapshot.error().message();
        EXPECT_EQ( snapshot.value().signer_set, std::vector<std::string>{ kBootstrapperAddress } );
        EXPECT_EQ( snapshot.value().required_signatures, 1u );

        // Forwarding overrides stay byte-compatible with the TPR's own state.
        EXPECT_EQ( registry_->BaseKey().GetKey(), kRegistryBaseKey );
        EXPECT_EQ( registry_->GetCurrentPeers(), kGenesisPeers );
    }

    TEST_F( PeerRegistryAssociationTest, SigChildKeyResolvesSameAuthority )
    {
        auto new_result = TrustedPeerRegistry::New( secure_crdt_, kGenesisPeers, kBootstrapperAddress, /*threshold=*/2 );
        ASSERT_TRUE( new_result.has_value() ) << new_result.error().message();
        registry_ = new_result.value();

        const auto base_entry  = secure_crdt_->Registry().Resolve( kRegistryBaseKey );
        const auto child_entry = secure_crdt_->Registry().Resolve(
            kRegistryBaseKey + "/sig/" + kBootstrapperAddress );
        ASSERT_TRUE( base_entry.has_value() );
        ASSERT_TRUE( child_entry.has_value() );
        EXPECT_EQ( child_entry->key_pattern, base_entry->key_pattern );
        ASSERT_TRUE( child_entry->peer_registry != nullptr );
        EXPECT_EQ( child_entry->peer_registry.get(), base_entry->peer_registry.get() )
            << "sig-child key must resolve to the same authority (T-15-04)";
    }

    TEST_F( PeerRegistryAssociationTest, MakeRegistrySignerSetSourceMatchesCurrentSignerSet )
    {
        auto new_result = TrustedPeerRegistry::New( secure_crdt_, kGenesisPeers, kBootstrapperAddress, /*threshold=*/2 );
        ASSERT_TRUE( new_result.has_value() ) << new_result.error().message();
        registry_ = new_result.value();

        sgns::securecrdt::SecureCrdtRegistryEntry entry;
        entry.signer_set_source = sgns::peerregistry::MakeRegistrySignerSetSource(
            std::static_pointer_cast<sgns::peerregistry::PeerRegistry>( registry_ ) );
        entry.make_instance = []() -> std::shared_ptr<sgns::securecrdt::ISignedCRDTData>
        { return std::make_shared<TestSignedData>(); };
        entry.owner_token   = &handmade_token_;
        entry.peer_registry = registry_; // shared_ptr upcasts to the PeerRegistry base
        secure_crdt_->Registry().Register( kHandmadeKey, std::move( entry ) );

        const auto resolved = secure_crdt_->Registry().Resolve( kHandmadeKey );
        ASSERT_TRUE( resolved.has_value() );
        ASSERT_TRUE( resolved->signer_set_source );

        const auto via_source    = resolved->signer_set_source( kHandmadeKey );
        const auto via_interface = registry_->CurrentSignerSet();
        ASSERT_FALSE( via_source.has_error() ) << via_source.error().message();
        ASSERT_FALSE( via_interface.has_error() ) << via_interface.error().message();
        EXPECT_EQ( via_source.value().signer_set, via_interface.value().signer_set );
        EXPECT_EQ( via_source.value().required_signatures, via_interface.value().required_signatures );
        ASSERT_TRUE( resolved->peer_registry != nullptr );
        EXPECT_EQ( resolved->peer_registry.get(), registry_.get() );
    }

    TEST_F( PeerRegistryAssociationTest, UnregisterRemovesAssociation )
    {
        auto new_result = TrustedPeerRegistry::New( secure_crdt_, kGenesisPeers, kBootstrapperAddress, /*threshold=*/2 );
        ASSERT_TRUE( new_result.has_value() ) << new_result.error().message();
        registry_ = new_result.value();
        ASSERT_TRUE( secure_crdt_->Registry().Resolve( kRegistryBaseKey ).has_value() );

        registry_->Unregister();

        EXPECT_FALSE( secure_crdt_->Registry().Resolve( kRegistryBaseKey ).has_value() );
        EXPECT_FALSE( secure_crdt_->Registry().Resolve(
                          kRegistryBaseKey + "/sig/" + kBootstrapperAddress )
                          .has_value() );
    }
} // namespace
