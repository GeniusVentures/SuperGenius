#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>
#include <TrustWalletCore/TWCurve.h>
#include <WalletCore/Hash.h>
#include <WalletCore/PrivateKey.h>
#include <ProofSystem/EthereumKeyGenerator.hpp>

#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/hash/algorithm/hash.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>

#include "account/GeniusAccount.hpp"
#include "account/GeniusSigner.hpp"
#include "base/hexutil.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/remove_all.hpp"

namespace
{
    using namespace sgns;

    constexpr char PRIVATE_KEY[] = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    /// Same re-encoding GeniusAccount performs when handing a crypto3 key to GeniusSigner.
    GeniusSigner::PrivateKey ToSecp256k1SecretKey( const ethereum::EthereumKeyGenerator &key )
    {
        GeniusSigner::PrivateKey secret_key{};
        nil::marshalling::bincode::field<ethereum::scalar_field_type>::field_element_to_bytes<
            GeniusSigner::PrivateKey::iterator>( key.get_private_key().private_key_data(),
                                                 secret_key.begin(),
                                                 secret_key.end() );
        std::reverse( secret_key.begin(), secret_key.end() );
        return secret_key;
    }

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

/// GeniusSigner no longer derives its address through crypto3. Pin the new
/// libsecp256k1 derivation against EthereumKeyGenerator so the switch cannot
/// silently change any existing account's address.
TEST( GeniusSignerTest, DerivesTheSameAddressAsEthereumKeyGenerator )
{
    const ethereum::EthereumKeyGenerator crypto3_key( PRIVATE_KEY );
    const GeniusSigner                   signer( ToSecp256k1SecretKey( crypto3_key ) );

    EXPECT_EQ( signer.GetAddress(), crypto3_key.GetEntirePubValue() );
    EXPECT_EQ( signer.GetAddress().size(), 128 );

    // The secret key round-trips to exactly the hex the caller supplied.
    EXPECT_EQ( base::hex_lower( gsl::make_span( ToSecp256k1SecretKey( crypto3_key ) ) ), PRIVATE_KEY );
}

/// A signature produced by the crypto3-free signer must still verify under
/// crypto3, and vice versa, for the very same address.
TEST( GeniusSignerTest, SignaturesInteroperateWithCrypto3 )
{
    const std::vector<uint8_t> message = { 'i', 'n', 't', 'e', 'r', 'o', 'p' };

    const ethereum::EthereumKeyGenerator crypto3_key( PRIVATE_KEY );
    const GeniusSigner                   signer( ToSecp256k1SecretKey( crypto3_key ) );

    const auto signer_signature = signer.Sign( message );
    ASSERT_EQ( signer_signature.size(), 64 );
    EXPECT_TRUE( VerifyWithCrypto3( signer.GetAddress(), signer_signature, message ) );

    const auto crypto3_signature = SignWithCrypto3( crypto3_key, message );
    EXPECT_TRUE( GeniusSigner::VerifySignature( signer.GetAddress(), crypto3_signature, message ) );
}

/// GeniusAccount now derives its address from the stored key seed with
/// boost::multiprecision + libsecp256k1 instead of crypto3. Reproduce the
/// legacy crypto3 derivation end to end and pin the account address to it, so
/// no already-provisioned wallet can silently resolve to a different address.
TEST_F( GeniusAccountSignatureTest, AccountAddressMatchesLegacyCrypto3SeedDerivation )
{
    // Legacy path: TW-sign the fixed ElGamal seed, SHA256 it, read the digest
    // big-endian as a 256-bit key seed, then derive the key through crypto3.
    const auto     private_key_bytes = base::unhex( PRIVATE_KEY ).value();
    TW::PrivateKey tw_private_key( private_key_bytes );

    const auto signed_secret = tw_private_key.sign( TW::Data( GeniusAccount::ELGAMAL_PUBKEY_PREDEFINED.cbegin(),
                                                              GeniusAccount::ELGAMAL_PUBKEY_PREDEFINED.cend() ),
                                                    TWCurveSECP256k1 );
    ASSERT_FALSE( signed_secret.empty() );

    const nil::crypto3::multiprecision::uint256_t key_seed( TW::Hash::sha256( signed_secret ) );
    const ethereum::EthereumKeyGenerator          legacy_key( key_seed );

    // New path: the same seed derivation, now free of crypto3.
    const auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), PRIVATE_KEY, path_ );
    ASSERT_NE( account, nullptr );

    ASSERT_EQ( account->GetAddress().size(), 128 ); // guard against an empty == empty pass
    EXPECT_EQ( account->GetAddress(), legacy_key.GetEntirePubValue() );

    // The signature the account produces must still verify under crypto3 at that address.
    const std::vector<uint8_t> message = { 's', 'e', 'e', 'd' };
    EXPECT_TRUE( VerifyWithCrypto3( account->GetAddress(), account->Sign( message ), message ) );
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
