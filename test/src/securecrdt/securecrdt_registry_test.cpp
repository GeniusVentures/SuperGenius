#include <gtest/gtest.h>

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

    const auto *resolved = SecureCrdtRegistry::Resolve( kTestKeyPattern );
    ASSERT_NE( resolved, nullptr );
    EXPECT_EQ( resolved->key_pattern, kTestKeyPattern );

    // sig/<addr> child of the base key resolves to the same entry.
    const auto *resolved_child = SecureCrdtRegistry::Resolve( std::string( kTestKeyPattern ) + "/sig/addr1" );
    ASSERT_NE( resolved_child, nullptr );
    EXPECT_EQ( resolved_child->key_pattern, kTestKeyPattern );

    // An unregistered key never resolves.
    EXPECT_EQ( SecureCrdtRegistry::Resolve( "gnus-other-key" ), nullptr );

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
    EXPECT_NE( SecureCrdtRegistry::Resolve( kTestKeyPattern ), nullptr );

    // Matching token: removes the entry.
    SecureCrdtRegistry::UnregisterIf( kTestKeyPattern, &correct_token_storage );
    EXPECT_EQ( SecureCrdtRegistry::Resolve( kTestKeyPattern ), nullptr );
}
