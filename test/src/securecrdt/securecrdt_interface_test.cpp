#include <gtest/gtest.h>

#include "securecrdt/ISignedCRDTData.hpp"

namespace
{
    using namespace sgns::securecrdt;

    /// @brief Trivial concrete ISignedCRDTData implementer with a
    ///        std::vector<uint8_t> payload, used to prove the interface
    ///        compiles and links with zero GlobalDB/CRDT/node dependency.
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
            applied_ = true;
        }

        std::vector<uint8_t> value_;
        bool                 applied_ = false;
    };
} // namespace

TEST( SecureCrdtInterfaceTest, VerifyAcceptsMatchingPayload )
{
    TestSignedData data;
    ASSERT_TRUE( data.DeserializeFromBytes( { 1, 2, 3 } ) );
    EXPECT_TRUE( data.Verify( { 1, 2, 3 } ) );
}

TEST( SecureCrdtInterfaceTest, VerifyRejectsTamperedPayload )
{
    TestSignedData data;
    ASSERT_TRUE( data.DeserializeFromBytes( { 1, 2, 3 } ) );
    EXPECT_FALSE( data.Verify( { 9, 9, 9 } ) );
}

TEST( SecureCrdtInterfaceTest, DeserializeFromBytesRejectsMalformedInput )
{
    TestSignedData data;
    EXPECT_FALSE( data.DeserializeFromBytes( {} ) );
}

TEST( SecureCrdtInterfaceTest, ApplySetsFlag )
{
    TestSignedData data;
    EXPECT_FALSE( data.applied_ );
    data.Apply();
    EXPECT_TRUE( data.applied_ );
}
