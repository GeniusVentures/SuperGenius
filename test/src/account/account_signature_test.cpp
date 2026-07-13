#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/GeniusAccount.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"

namespace
{
    using namespace sgns;

    constexpr char PRIVATE_KEY[] = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    class GeniusAccountSignatureTest : public ::testing::Test
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

TEST_F( GeniusAccountSignatureTest, VerifiesExistingSignatureFormat )
{
    auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), PRIVATE_KEY, path_ );
    ASSERT_NE( account, nullptr );

    const std::vector<uint8_t> message = { 'S', 'u', 'p', 'e', 'r', 'G', 'e', 'n', 'i', 'u', 's' };
    auto                       signature = account->Sign( message );

    ASSERT_TRUE( GeniusAccount::VerifySignature(
        account->GetAddress(),
        std::string_view( reinterpret_cast<const char *>( signature.data() ), signature.size() ),
        message ) );

    signature.front() ^= 1;
    EXPECT_FALSE( GeniusAccount::VerifySignature(
        account->GetAddress(),
        std::string_view( reinterpret_cast<const char *>( signature.data() ), signature.size() ),
        message ) );
    EXPECT_FALSE( GeniusAccount::VerifySignature( account->GetAddress(), "short", message ) );
    EXPECT_FALSE( GeniusAccount::VerifySignature( "not a public key", std::string( 64, '\0' ), message ) );
}
