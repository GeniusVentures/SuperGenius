#include "GeniusAccount.hpp"

#include "ProofSystem/ElGamalKeyGenerator.hpp"
#include "ProofSystem/EthereumKeyGenerator.hpp"
#include "ProofSystem/KDFGenerator.hpp"
#include "local_secure_storage/ISecureStorage.hpp"
#include "singleton/CComponentFactory.hpp"
#include <nil/crypto3/pubkey/algorithm/sign.hpp>

outcome::result<KeyGenerator::ElGamal> sgns::GeniusAccount::GenerateGeniusAddress( std::string_view base_path,
                                                                                   std::string_view eth_private_key )
{
    auto component_factory = SINGLETONINSTANCE( CComponentFactory );
    OUTCOME_TRY( ( auto &&, icomponent ), component_factory->GetComponent( "LocalSecureStorage" ) );

    auto secure_storage = std::dynamic_pointer_cast<ISecureStorage>( icomponent );
    auto load_res       = secure_storage->Load( "sgns_key", std::string( base_path ) );

    nil::crypto3::multiprecision::uint256_t key_seed;
    if ( load_res )
    {
        key_seed = nil::crypto3::multiprecision::uint256_t( load_res.value() );
    }
    else
    {
        ethereum::EthereumKeyGenerator private_key( eth_private_key );

        KeyGenerator::ElGamal elgamal_pubkey_predefined( 0x1234_cppui256 );

        auto fixed_public_key = elgamal_pubkey_predefined.GetPublicKey().public_key_value.str();

        nil::crypto3::pubkey::public_key<ethereum::policy_type>::signature_type signed_secret =
            nil::crypto3::sign<ethereum::policy_type>( fixed_public_key, private_key.get_private_key() );

        std::vector<std::uint8_t> signed_vector( 64 );

        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( std::get<0>( signed_secret ),
                                                  signed_vector.begin(),
                                                  signed_vector.begin() + signed_vector.size() / 2 );
        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( std::get<1>( signed_secret ),
                                                  signed_vector.begin() + signed_vector.size() / 2,
                                                  signed_vector.end() );

        std::array<uint8_t, 32> hashed =
            nil::crypto3::hash<ecdsa_t::hashes::sha2<256>>( signed_vector.cbegin(), signed_vector.cend() );

        key_seed = nil::crypto3::multiprecision::uint256_t( hashed );

        BOOST_OUTCOME_TRYV2( auto &&, secure_storage->Save( "sgns_key", key_seed.str(), std::string( base_path ) ) );
    }

    KeyGenerator::ElGamal sgnus_key( key_seed );

    return sgnus_key;
}
