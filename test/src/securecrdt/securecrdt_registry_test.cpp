#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#include "securecrdt/ISignedCRDTData.hpp"
#include "securecrdt/SecureCrdtRegistry.hpp"

namespace
{
    using namespace sgns::securecrdt;

    class TestSignedData : public ISignedCRDTData
    {
    public:
        std::vector<uint8_t> SerializeToBytes() const override
        {
            return value_;
        }

        bool DeserializeFromBytes( const std::vector<uint8_t> &bytes ) override
        {
            if ( bytes.empty() )
            {
                return false;
            }
            value_ = bytes;
            return true;
        }

        bool Verify( const std::vector<uint8_t> &payload ) const override
        {
            return payload == value_;
        }

        void Apply() override
        {
        }

        std::vector<uint8_t> value_;
    };

    constexpr const char *kTestKeyPattern = "gnus-test-key";

    // Distinct opaque tokens used to exercise the compare-and-remove idiom.
    int correct_token_storage = 0;
    int wrong_token_storage   = 0;
} // namespace

TEST( SecureCrdtRegistryTest, RegisterAndResolveRoundTrip )
{
    SecureCrdtRegistry registry;
    SecureCrdtRegistryEntry entry;
    entry.signer_set_source = []( const std::string & ) -> outcome::result<SignerSetSnapshot>
    {
        return SignerSetSnapshot{ { "addr1", "addr2" }, 2 };
    };
    entry.make_instance = [] { return std::make_shared<TestSignedData>(); };
    entry.owner_token    = &correct_token_storage;

    registry.Register( kTestKeyPattern, entry );

    const auto resolved = registry.Resolve( kTestKeyPattern );
    ASSERT_TRUE( resolved.has_value() );
    EXPECT_EQ( resolved->key_pattern, kTestKeyPattern );

    // sig/<addr> child of the base key resolves to the same entry.
    const auto resolved_child = registry.Resolve( std::string( kTestKeyPattern ) + "/sig/addr1" );
    ASSERT_TRUE( resolved_child.has_value() );
    EXPECT_EQ( resolved_child->key_pattern, kTestKeyPattern );

    EXPECT_FALSE( registry.Resolve( std::string( kTestKeyPattern ) + "/sig/" ).has_value() );
    EXPECT_FALSE( registry.Resolve( std::string( kTestKeyPattern ) + "/sig/addr1/extra" ).has_value() );

    // An unregistered key never resolves.
    EXPECT_FALSE( registry.Resolve( "gnus-other-key" ).has_value() );

    registry.UnregisterIf( kTestKeyPattern, &correct_token_storage );
}

TEST( SecureCrdtRegistryTest, UnregisterIfRequiresMatchingToken )
{
    SecureCrdtRegistry registry;
    SecureCrdtRegistryEntry entry;
    entry.signer_set_source = []( const std::string & ) -> outcome::result<SignerSetSnapshot>
    {
        return SignerSetSnapshot{ { "addr1", "addr2" }, 2 };
    };
    entry.make_instance = [] { return std::make_shared<TestSignedData>(); };
    entry.owner_token    = &correct_token_storage;

    registry.Register( kTestKeyPattern, entry );

    // Mismatched token: no-op.
    registry.UnregisterIf( kTestKeyPattern, &wrong_token_storage );
    EXPECT_TRUE( registry.Resolve( kTestKeyPattern ).has_value() );

    // Matching token: removes the entry.
    registry.UnregisterIf( kTestKeyPattern, &correct_token_storage );
    EXPECT_FALSE( registry.Resolve( kTestKeyPattern ).has_value() );
}

TEST( SecureCrdtRegistryTest, ResolvedEntryRemainsValidAfterUnregister )
{
    SecureCrdtRegistry registry;
    SecureCrdtRegistryEntry entry;
    entry.signer_set_source = []( const std::string & ) -> outcome::result<SignerSetSnapshot>
    { return SignerSetSnapshot{ { "addr1" }, 1 }; };
    entry.make_instance = [] { return std::make_shared<TestSignedData>(); };
    entry.owner_token    = &correct_token_storage;

    registry.Register( kTestKeyPattern, entry );
    const auto resolved = registry.Resolve( kTestKeyPattern );
    ASSERT_TRUE( resolved.has_value() );

    registry.UnregisterIf( kTestKeyPattern, &correct_token_storage );

    EXPECT_EQ( resolved->key_pattern, kTestKeyPattern );
    EXPECT_NE( resolved->make_instance(), nullptr );
}

TEST( SecureCrdtRegistryTest, RegistriesWithSamePatternRemainIsolated )
{
    SecureCrdtRegistry registry_a;
    SecureCrdtRegistry registry_b;
    int                owner_a = 0;
    int                owner_b = 0;

    SecureCrdtRegistryEntry entry_a;
    entry_a.signer_set_source = []( const std::string & ) -> outcome::result<SignerSetSnapshot>
    { return SignerSetSnapshot{ { "node-a-signer" }, 1 }; };
    entry_a.make_instance = [] { return std::make_shared<TestSignedData>(); };
    entry_a.owner_token    = &owner_a;

    SecureCrdtRegistryEntry entry_b;
    entry_b.signer_set_source = []( const std::string & ) -> outcome::result<SignerSetSnapshot>
    { return SignerSetSnapshot{ { "node-b-signer" }, 1 }; };
    entry_b.make_instance = [] { return std::make_shared<TestSignedData>(); };
    entry_b.owner_token    = &owner_b;

    registry_a.Register( kTestKeyPattern, std::move( entry_a ) );
    registry_b.Register( kTestKeyPattern, std::move( entry_b ) );

    const auto resolved_a = registry_a.Resolve( kTestKeyPattern );
    const auto resolved_b = registry_b.Resolve( kTestKeyPattern );
    ASSERT_TRUE( resolved_a.has_value() );
    ASSERT_TRUE( resolved_b.has_value() );

    const auto snapshot_a = resolved_a->signer_set_source( kTestKeyPattern );
    const auto snapshot_b = resolved_b->signer_set_source( kTestKeyPattern );
    ASSERT_FALSE( snapshot_a.has_error() );
    ASSERT_FALSE( snapshot_b.has_error() );
    EXPECT_EQ( snapshot_a.value().signer_set, std::vector<std::string>{ "node-a-signer" } );
    EXPECT_EQ( snapshot_b.value().signer_set, std::vector<std::string>{ "node-b-signer" } );

    registry_b.UnregisterIf( kTestKeyPattern, &owner_b );
    EXPECT_TRUE( registry_a.Resolve( kTestKeyPattern ).has_value() );
    EXPECT_FALSE( registry_b.Resolve( kTestKeyPattern ).has_value() );
}

TEST( SecureCrdtRegistryTest, ConcurrentAccessUsesStableSnapshots )
{
    constexpr size_t thread_count = 8;
    constexpr size_t iterations   = 200;

    std::array<int, thread_count> owner_tokens{};
    std::atomic_bool              failed{ false };
    std::vector<std::thread>      threads;
    SecureCrdtRegistry            registry;
    threads.reserve( thread_count );

    for ( size_t thread_index = 0; thread_index < thread_count; ++thread_index )
    {
        threads.emplace_back(
            [thread_index, &owner_tokens, &failed, &registry]()
            {
                const std::string key = "gnus-concurrent-key-" + std::to_string( thread_index );
                for ( size_t iteration = 0; iteration < iterations; ++iteration )
                {
                    SecureCrdtRegistryEntry entry;
                    entry.signer_set_source = []( const std::string & ) -> outcome::result<SignerSetSnapshot>
                    { return SignerSetSnapshot{ { "addr1" }, 1 }; };
                    entry.make_instance = [] { return std::make_shared<TestSignedData>(); };
                    entry.owner_token    = &owner_tokens[thread_index];

                    registry.Register( key, std::move( entry ) );
                    const auto resolved = registry.Resolve( key );
                    if ( !resolved.has_value() || resolved->owner_token != &owner_tokens[thread_index] )
                    {
                        failed.store( true );
                    }
                    (void)registry.AllEntries();
                    registry.UnregisterIf( key, &owner_tokens[thread_index] );
                }
            } );
    }

    for ( auto &thread : threads )
    {
        thread.join();
    }

    EXPECT_FALSE( failed.load() );
}
