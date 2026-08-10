#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>
#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/hash/algorithm/hash.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>

#include "account/GeniusAccount.hpp"
#include "account/GeniusSigner.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/remove_all.hpp"

namespace
{
    using namespace sgns;

    constexpr char PRIVATE_KEY[] = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    std::vector<uint8_t> SignWithCrypto3( const ethereum::EthereumKeyGenerator &key, const std::vector<uint8_t> &data )
    {
        const std::array<uint8_t, 32>  hash      = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( data );
        const ethereum::signature_type signature = nil::crypto3::sign( hash, key.get_private_key() );

        std::vector<uint8_t> bytes( 64 );
        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<uint8_t>::iterator>( std::get<0>( signature ), bytes.begin(), bytes.begin() + 32 );
        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<uint8_t>::iterator>( std::get<1>( signature ), bytes.begin() + 32, bytes.end() );
        return bytes;
    }

    bool VerifyWithCrypto3( const std::string          &address,
                            const std::vector<uint8_t> &signature,
                            const std::vector<uint8_t> &data )
    {
        auto [r_ok, r] = nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_from_bytes(
            signature.begin(),
            signature.begin() + 32 );
        auto [s_ok, s] = nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_from_bytes(
            signature.begin() + 32,
            signature.end() );
        if ( !r_ok || !s_ok )
        {
            return false;
        }

        const std::array<uint8_t, 32> hash = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( data );
        return nil::crypto3::verify( hash,
                                     ethereum::signature_type( r, s ),
                                     ethereum::EthereumKeyGenerator::BuildPublicKey( address ) );
    }

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
            test::removeAllWithRetry( path_.string() );
        }

        boost::filesystem::path path_;
    };
} // namespace

TEST_F( GeniusAccountSignatureTest, VerifiesExistingSignatureFormat )
{
    auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), PRIVATE_KEY, path_ );
    ASSERT_NE( account, nullptr );

    const std::vector<uint8_t> message   = { 'S', 'u', 'p', 'e', 'r', 'G', 'e', 'n', 'i', 'u', 's' };
    auto                       signature = account->Sign( message );

    ASSERT_TRUE( GeniusAccount::VerifySignature( account->GetAddress(), signature, message ) );

    signature.front() ^= 1;
    EXPECT_FALSE( GeniusAccount::VerifySignature( account->GetAddress(), signature, message ) );
    EXPECT_FALSE( GeniusAccount::VerifySignature( account->GetAddress(), "short", message ) );
    EXPECT_FALSE( GeniusAccount::VerifySignature( "not a public key", std::string( 64, '\0' ), message ) );
}

TEST_F( GeniusAccountSignatureTest, EphemeralAccountSignsWithoutInvokingPersistenceFactory )
{
    size_t storage_factory_calls = 0;
    GeniusAccount::SetSecureStorageFactory(
        [&storage_factory_calls]( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
        {
            ++storage_factory_calls;
            return std::make_shared<MemorySecureStorage>( identifier );
        } );

    auto account = GeniusAccount::NewEphemeral( TokenID::FromBytes( { 0x00 } ) );
    ASSERT_NE( account, nullptr );

    const std::vector<uint8_t> message   = { 'e', 'p', 'h', 'e', 'm', 'e', 'r', 'a', 'l' };
    const auto                 signature = account->Sign( message );

    EXPECT_TRUE( GeniusAccount::VerifySignature( account->GetAddress(), signature, message ) );
    EXPECT_EQ( storage_factory_calls, 0 );
    EXPECT_FALSE( boost::filesystem::exists( path_ / "secure_storage_id" ) );
}

TEST( GeniusSignerTest, GeneratedSignerUsesCanonicalAccountSignatureFormat )
{
    auto signer = GeniusSigner::Generate();

    const std::vector<uint8_t> message   = { 's', 'i', 'g', 'n', 'e', 'r' };
    const auto                 signature = signer.Sign( message );

    ASSERT_EQ( signature.size(), 64 );
    EXPECT_TRUE( GeniusSigner::VerifySignature( signer.GetAddress(), signature, message ) );
    EXPECT_TRUE( GeniusAccount::VerifySignature( signer.GetAddress(), signature, message ) );
}

TEST_F( GeniusAccountSignatureTest, Crypto3AndLibsecp256k1AreCompatible )
{
    const std::vector<uint8_t> message = { 'c', 'o', 'm', 'p', 'a', 't', 'i', 'b', 'i', 'l', 'i', 't', 'y' };

    const ethereum::EthereumKeyGenerator crypto3_key( PRIVATE_KEY );
    const auto                           crypto3_signature = SignWithCrypto3( crypto3_key, message );
    EXPECT_TRUE( GeniusAccount::VerifySignature( crypto3_key.GetEntirePubValue(), crypto3_signature, message ) );

    const auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), PRIVATE_KEY, path_ );
    ASSERT_NE( account, nullptr );
    const auto libsecp256k1_signature = account->Sign( message );
    ASSERT_EQ( libsecp256k1_signature.size(), 64 );
    EXPECT_TRUE( VerifyWithCrypto3( account->GetAddress(), libsecp256k1_signature, message ) );
}
