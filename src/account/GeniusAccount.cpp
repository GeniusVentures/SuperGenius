#include "GeniusAccount.hpp"

#include "ProofSystem/ElGamalKeyGenerator.hpp"
#include "ProofSystem/EthereumKeyGenerator.hpp"
#include "ProofSystem/KDFGenerator.hpp"
#include "local_secure_storage/ISecureStorage.hpp"
#include "singleton/CComponentFactory.hpp"
#include <nil/crypto3/pubkey/algorithm/sign.hpp>

outcome::result<KeyGenerator::ElGamal> sgns::GeniusAccount::GenerateGeniusAddress( const std::string &base_path,
                                                                                   const char        *eth_private_key )
{
    auto component_factory = SINGLETONINSTANCE( CComponentFactory );
    OUTCOME_TRY( ( auto &&, icomponent ), component_factory->GetComponent( "LocalSecureStorage" ) );

    auto secure_storage = std::dynamic_pointer_cast<ISecureStorage>( icomponent );
    auto load_res       = secure_storage->Load( "sgns_key", base_path );

    nil::crypto3::multiprecision::uint256_t key_seed;
    if ( load_res )
    {
        key_seed = nil::crypto3::multiprecision::uint256_t( load_res.value() );
        // for ( size_t i = 0; i < load_res.value().size(); ++i )
        // {
        //     key_seed[i] = static_cast<uint8_t>( load_res.value()[i] );
        // }
    }
    else
    {
        ethereum::EthereumKeyGenerator private_key( eth_private_key );

        KeyGenerator::ElGamal elgamal_pubkey_predefined( 0x1234_cppui256 );

        auto signed_secret =
            nil::crypto3::sign<ethereum::policy_type>( elgamal_pubkey_predefined, private_key.get_private_key() );

        auto hashed = nil::crypto3::hash<ecdsa_t::hashes::sha2<256>>( signed_secret );

        BOOST_OUTCOME_TRYV2( auto &&, secure_storage->Save( "sgns_key", hashed, base_path ) );
        key_seed = hashed;
    }

    KeyGenerator::ElGamal sgnus_key( key_seed );

    return sgnus_key;
}
