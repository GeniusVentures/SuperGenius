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
    SecureCrdtRegistryEntry entry;
    entry.signer_set_source = []( const std::string & ) -> outcome::result<SignerSetSnapshot>
    {
        return SignerSetSnapshot{ { "addr1", "addr2" }, 2 };
    };
    entry.make_instance = [] { return std::make_shared<TestSignedData>(); };
    entry.owner_token    = &correct_token_storage;

    SecureCrdtRegistry::Register( kTestKeyPattern, entry );

    const auto resolved = SecureCrdtRegistry::Resolve( kTestKeyPattern );
    ASSERT_TRUE( resolved.has_value() );
    EXPECT_EQ( resolved->key_pattern, kTestKeyPattern );

    // sig/<addr> child of the base key resolves to the same entry.
    const auto resolved_child = SecureCrdtRegistry::Resolve( std::string( kTestKeyPattern ) + "/sig/addr1" );
    ASSERT_TRUE( resolved_child.has_value() );
    EXPECT_EQ( resolved_child->key_pattern, kTestKeyPattern );

    EXPECT_FALSE( SecureCrdtRegistry::Resolve( std::string( kTestKeyPattern ) + "/sig/" ).has_value() );
    EXPECT_FALSE(
        SecureCrdtRegistry::Resolve( std::string( kTestKeyPattern ) + "/sig/addr1/extra" ).has_value() );

    // An unregistered key never resolves.
    EXPECT_FALSE( SecureCrdtRegistry::Resolve( "gnus-other-key" ).has_value() );

    SecureCrdtRegistry::UnregisterIf( kTestKeyPattern, &correct_token_storage );
}

TEST( SecureCrdtRegistryTest, UnregisterIfRequiresMatchingToken )
{
    SecureCrdtRegistryEntry entry;
    entry.signer_set_source = []( const std::string & ) -> outcome::result<SignerSetSnapshot>
    {
        return SignerSetSnapshot{ { "addr1", "addr2" }, 2 };
    };
    entry.make_instance = [] { return std::make_shared<TestSignedData>(); };
    entry.owner_token    = &correct_token_storage;

    SecureCrdtRegistry::Register( kTestKeyPattern, entry );

    // Mismatched token: no-op.
    SecureCrdtRegistry::UnregisterIf( kTestKeyPattern, &wrong_token_storage );
    EXPECT_TRUE( SecureCrdtRegistry::Resolve( kTestKeyPattern ).has_value() );

    // Matching token: removes the entry.
    SecureCrdtRegistry::UnregisterIf( kTestKeyPattern, &correct_token_storage );
    EXPECT_FALSE( SecureCrdtRegistry::Resolve( kTestKeyPattern ).has_value() );
}

TEST( SecureCrdtRegistryTest, ResolvedEntryRemainsValidAfterUnregister )
{
    SecureCrdtRegistryEntry entry;
    entry.signer_set_source = []( const std::string & ) -> outcome::result<SignerSetSnapshot>
    { return SignerSetSnapshot{ { "addr1" }, 1 }; };
    entry.make_instance = [] { return std::make_shared<TestSignedData>(); };
    entry.owner_token    = &correct_token_storage;

    SecureCrdtRegistry::Register( kTestKeyPattern, entry );
    const auto resolved = SecureCrdtRegistry::Resolve( kTestKeyPattern );
    ASSERT_TRUE( resolved.has_value() );

    SecureCrdtRegistry::UnregisterIf( kTestKeyPattern, &correct_token_storage );

    EXPECT_EQ( resolved->key_pattern, kTestKeyPattern );
    EXPECT_NE( resolved->make_instance(), nullptr );
}

TEST( SecureCrdtRegistryTest, ConcurrentAccessUsesStableSnapshots )
{
    constexpr size_t thread_count = 8;
    constexpr size_t iterations   = 200;

    std::array<int, thread_count> owner_tokens{};
    std::atomic_bool              failed{ false };
    std::vector<std::thread>      threads;
    threads.reserve( thread_count );

    for ( size_t thread_index = 0; thread_index < thread_count; ++thread_index )
    {
        threads.emplace_back(
            [thread_index, &owner_tokens, &failed]()
            {
                const std::string key = "gnus-concurrent-key-" + std::to_string( thread_index );
                for ( size_t iteration = 0; iteration < iterations; ++iteration )
                {
                    SecureCrdtRegistryEntry entry;
                    entry.signer_set_source = []( const std::string & ) -> outcome::result<SignerSetSnapshot>
                    { return SignerSetSnapshot{ { "addr1" }, 1 }; };
                    entry.make_instance = [] { return std::make_shared<TestSignedData>(); };
                    entry.owner_token    = &owner_tokens[thread_index];

                    SecureCrdtRegistry::Register( key, std::move( entry ) );
                    const auto resolved = SecureCrdtRegistry::Resolve( key );
                    if ( !resolved.has_value() || resolved->owner_token != &owner_tokens[thread_index] )
                    {
                        failed.store( true );
                    }
                    (void)SecureCrdtRegistry::AllEntries();
                    SecureCrdtRegistry::UnregisterIf( key, &owner_tokens[thread_index] );
                }
            } );
    }

    for ( auto &thread : threads )
    {
        thread.join();
    }

    EXPECT_FALSE( failed.load() );
}
