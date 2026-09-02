/**
 * @file       validator_registry_scope_test.cpp
 * @brief      Instance-scope ValidatorRegistry identifier tests (D-09).
 * @details    Pins the two structural-isolation properties of the scoped
 *             validator registry: (1) the public instance's registry key,
 *             gossip topic, and CID key are byte-identical to the retained
 *             static constants, and (2) two registries with different network
 *             scopes read/write disjoint CRDT keys and gossip topics, so
 *             consensus can never merge across networks (Pitfall 7). Pure
 *             identifier-level checks — no consensus round is needed.
 * @date       2026-09-02
 */

#include <gtest/gtest.h>

#include "blockchain/ValidatorRegistry.hpp"
#include "testutil/storage/base_crdt_test.hpp"

#include <functional>
#include <string>
#include <unordered_set>

namespace
{
    // Public identifiers pinned to the exact historical literals (T-15-23).
    constexpr std::string_view kPublicRegistryKey = "gnus-validator-registry";
    constexpr std::string_view kPublicTopic       = "gnus-validator-registry";
    constexpr std::string_view kPublicCidKey      = "gnus-validator-registry-cid";

    // Realistic private-network identities (0x-prefixed hex of exactly 32 bytes).
    const std::string kScopeA = "0x" + std::string( 64, 'a' );
    const std::string kScopeB = "0x" + std::string( 64, 'b' );

    const std::string kValidatorId = "validator-registry-scope-test";

    std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry( const std::shared_ptr<sgns::crdt::GlobalDB> &db,
                                                           const std::string                           &network_scope )
    {
        auto registry = sgns::ValidatorRegistry::New(
            db,
            1,
            1,
            sgns::ValidatorRegistry::WeightConfig{},
            kValidatorId,
            []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
            { cb( outcome::failure( std::errc::not_supported ) ); },
            nullptr,
            network_scope );
        EXPECT_TRUE( registry );
        return registry;
    }
} // namespace

// Compile-time proof that the retained static constants are unchanged (T-15-23).
static_assert( sgns::ValidatorRegistry::RegistryKey() == kPublicRegistryKey );
static_assert( sgns::ValidatorRegistry::ValidatorTopic() == kPublicTopic );
static_assert( sgns::ValidatorRegistry::RegistryCidKey() == kPublicCidKey );

class ValidatorRegistryScopeTest : public test::CRDTFixture
{
public:
    ValidatorRegistryScopeTest() : CRDTFixture( "validator_registry_scope_test" )
    {
    }
};

/** @brief A default-scope registry exposes exactly the public identifiers. */
TEST_F( ValidatorRegistryScopeTest, PublicScopeByteStable )
{
    auto registry = MakeRegistry( db_, "" );
    ASSERT_TRUE( registry );

    EXPECT_EQ( registry->RegistryKeyValue(), kPublicRegistryKey );
    EXPECT_EQ( registry->ValidatorTopicValue(), kPublicTopic );
    EXPECT_EQ( registry->RegistryCidKeyValue(), kPublicCidKey );

    // Instance values equal the retained static constants byte for byte.
    EXPECT_EQ( registry->RegistryKeyValue(), std::string( sgns::ValidatorRegistry::RegistryKey() ) );
    EXPECT_EQ( registry->ValidatorTopicValue(), std::string( sgns::ValidatorRegistry::ValidatorTopic() ) );
    EXPECT_EQ( registry->RegistryCidKeyValue(), std::string( sgns::ValidatorRegistry::RegistryCidKey() ) );
}

/** @brief A scoped registry suffixes every identifier; none equals the public value. */
TEST_F( ValidatorRegistryScopeTest, PrivateScopeSuffixed )
{
    auto registry = MakeRegistry( db_, kScopeA );
    ASSERT_TRUE( registry );

    const std::string expected_suffix = "/" + kScopeA;
    EXPECT_EQ( registry->RegistryKeyValue(), std::string( kPublicRegistryKey ) + expected_suffix );
    EXPECT_EQ( registry->ValidatorTopicValue(), std::string( kPublicTopic ) + expected_suffix );
    EXPECT_EQ( registry->RegistryCidKeyValue(), std::string( kPublicCidKey ) + expected_suffix );

    EXPECT_NE( registry->RegistryKeyValue(), kPublicRegistryKey );
    EXPECT_NE( registry->ValidatorTopicValue(), kPublicTopic );
    EXPECT_NE( registry->RegistryCidKeyValue(), kPublicCidKey );
}

/**
 * @brief Public, scopeA, and scopeB produce three-way disjoint identifier sets.
 * @details Two registries whose scopes differ never share a CRDT key, gossip
 *          topic, or CID key — the structural isolation D-09 requires (a
 *          second registry sharing the public identifiers would silently
 *          merge the networks' consensus, Pitfall 7 / T-15-22).
 */
TEST_F( ValidatorRegistryScopeTest, DisjointScopes )
{
    auto public_registry = MakeRegistry( db_, "" );
    auto registry_a      = MakeRegistry( db_, kScopeA );
    auto registry_b      = MakeRegistry( db_, kScopeB );
    ASSERT_TRUE( public_registry );
    ASSERT_TRUE( registry_a );
    ASSERT_TRUE( registry_b );

    const std::unordered_set<std::string> registry_keys{
        public_registry->RegistryKeyValue(), registry_a->RegistryKeyValue(), registry_b->RegistryKeyValue()
    };
    const std::unordered_set<std::string> topics{
        public_registry->ValidatorTopicValue(), registry_a->ValidatorTopicValue(), registry_b->ValidatorTopicValue()
    };
    const std::unordered_set<std::string> cid_keys{
        public_registry->RegistryCidKeyValue(), registry_a->RegistryCidKeyValue(), registry_b->RegistryCidKeyValue()
    };

    // Three distinct scopes must yield three distinct values per identifier kind.
    EXPECT_EQ( registry_keys.size(), 3U );
    EXPECT_EQ( topics.size(), 3U );
    EXPECT_EQ( cid_keys.size(), 3U );
}

/** @brief The static constexpr defaults still return the original literals. */
TEST_F( ValidatorRegistryScopeTest, StaticDefaultsPreserved )
{
    // Runtime mirror of the file-level static_asserts: the static constants are
    // the compile-time public contract and must never drift (T-15-23).
    EXPECT_EQ( sgns::ValidatorRegistry::RegistryKey(), kPublicRegistryKey );
    EXPECT_EQ( sgns::ValidatorRegistry::ValidatorTopic(), kPublicTopic );
    EXPECT_EQ( sgns::ValidatorRegistry::RegistryCidKey(), kPublicCidKey );
}
