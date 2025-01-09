#include "GeniusAccount.hpp"

#include "ProofSystem/ElGamalKeyGenerator.hpp"
#include "ProofSystem/EthereumKeyGenerator.hpp"
#include "ProofSystem/KDFGenerator.hpp"
#include "WalletCore/Hash.h"
#include "local_secure_storage/ISecureStorage.hpp"
#include "nil/crypto3/multiprecision/cpp_int_modular/import_export.hpp"
#include "singleton/CComponentFactory.hpp"
#include "WalletCore/PrivateKey.h"
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <boost/algorithm/hex.hpp>

using namespace boost::multiprecision::literals;

namespace
{
    std::array<uint8_t, 32> get_elgamal_pubkey()
    {
        const cpp_int           elgamal_key = KeyGenerator::ElGamal( 0x1234_cppui256 ).GetPublicKey().public_key_value;
        std::array<uint8_t, 32> exported;
        export_bits( elgamal_key, exported.begin(), 8, false );

        return exported;
    }
}

namespace sgns
{
    const std::array<uint8_t, 32> GeniusAccount::ELGAMAL_PUBKEY_PREDEFINED = get_elgamal_pubkey();

    GeniusAccount::GeniusAccount( const uint8_t token_type, std::string_view base_path, const char *eth_private_key ) :
        token( token_type ), nonce( 0 )
    {
        if ( auto maybe_address = GenerateGeniusAddress( base_path, eth_private_key ); maybe_address.has_value() )
        {
            address = maybe_address.value();
        }
        else
        {
            std::cerr << "Could not get SGNS address: " << maybe_address.error().message() << std::endl;
            throw std::runtime_error( maybe_address.error().message() );
        }

        //TODO - Retrieve values where? Read through blockchain Here?
        // How to deal with errors?
    }

    outcome::result<KeyGenerator::ElGamal> GeniusAccount::GenerateGeniusAddress( std::string_view base_path,
                                                                                 const char      *eth_private_key )
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );
        OUTCOME_TRY( ( auto &&, icomponent ), component_factory->GetComponent( "LocalSecureStorage" ) );

        auto secure_storage = std::dynamic_pointer_cast<ISecureStorage>( icomponent );
        auto load_res       = secure_storage->Load( "sgns_key", std::string( base_path ) );

        boost::multiprecision::uint256_t key_seed;
        if ( load_res )
        {
            key_seed = boost::multiprecision::uint256_t( load_res.value() );
        }
        else
        {
            if ( eth_private_key == nullptr )
            {
                return outcome::failure( std::errc::invalid_argument );
            }

            std::vector<uint8_t> private_key_bytes;
            boost::algorithm::unhex(eth_private_key, eth_private_key + strlen(eth_private_key), std::back_inserter(private_key_bytes));

            if (private_key_bytes.size() != TW::PrivateKey::_size) {
                return outcome::failure(std::make_error_code(std::errc::invalid_argument)); 
            }

            TW::PrivateKey private_key( private_key_bytes );
            auto signed_secret = private_key.sign(
                TW::Data( ELGAMAL_PUBKEY_PREDEFINED.cbegin(), ELGAMAL_PUBKEY_PREDEFINED.cend() ),
                TWCurveSECP256k1 );

            if ( signed_secret.empty() )
            {
                std::cerr << "Could not sign secret\n";
                return outcome::failure( std::errc::invalid_argument );
            }

            auto hashed = TW::Hash::sha256( signed_secret );

            key_seed = boost::multiprecision::uint256_t( hashed );

            BOOST_OUTCOME_TRYV2( auto &&,
                                 secure_storage->Save( "sgns_key", key_seed.str(), std::string( base_path ) ) );
        }

        KeyGenerator::ElGamal sgnus_key( key_seed );

        return sgnus_key;
    }
}
