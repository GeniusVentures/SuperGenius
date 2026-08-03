#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/GeniusAccount.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "multisig/MultiSig.hpp"

namespace
{
    using namespace sgns;

    constexpr char PRIVATE_KEY[] = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    class MultiSigVerifyTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        }

        void TearDown() override
        {
            GeniusAccount::SetSecureStorageFactory( nullptr );
            boost::filesystem::remove_all( path_ );
        }

        boost::filesystem::path path_;
    };
} // namespace

TEST_F( MultiSigVerifyTest, ValidSignatureVerifiesTrue )
{
    auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), PRIVATE_KEY, path_ );
    ASSERT_NE( account, nullptr );

    const std::vector<uint8_t> payload = { 'p', 'a', 'y', 'l', 'o', 'a', 'd' };
    const auto                 signature = account->Sign( payload );

    EXPECT_TRUE( multisig::VerifyPayloadSignature( account->GetAddress(), signature, payload ) );
}

TEST_F( MultiSigVerifyTest, TamperedPayloadVerifiesFalse )
{
    auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), PRIVATE_KEY, path_ );
    ASSERT_NE( account, nullptr );

    const std::vector<uint8_t> payload          = { 'p', 'a', 'y', 'l', 'o', 'a', 'd' };
    const std::vector<uint8_t> tampered_payload  = { 'p', 'a', 'y', 'l', 'o', 'a', 'X' };
    const auto                 signature        = account->Sign( payload );

    EXPECT_FALSE( multisig::VerifyPayloadSignature( account->GetAddress(), signature, tampered_payload ) );
}

TEST_F( MultiSigVerifyTest, TamperedSignatureVerifiesFalse )
{
    auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), PRIVATE_KEY, path_ );
    ASSERT_NE( account, nullptr );

    const std::vector<uint8_t> payload   = { 'p', 'a', 'y', 'l', 'o', 'a', 'd' };
    auto                        signature = account->Sign( payload );
    signature.front() ^= 1;

    EXPECT_FALSE( multisig::VerifyPayloadSignature( account->GetAddress(), signature, payload ) );
}

TEST_F( MultiSigVerifyTest, WrongSizeSignatureVerifiesFalse )
{
    auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), PRIVATE_KEY, path_ );
    ASSERT_NE( account, nullptr );

    const std::vector<uint8_t> payload = { 'p', 'a', 'y', 'l', 'o', 'a', 'd' };

    EXPECT_FALSE( multisig::VerifyPayloadSignature(
        account->GetAddress(), std::vector<uint8_t>{ 's', 'h', 'o', 'r', 't' }, payload ) );
}
