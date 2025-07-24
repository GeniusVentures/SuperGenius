#include "GeniusAccount.hpp"

#include "WalletCore/Hash.h"
#include "local_secure_storage/ISecureStorage.hpp"
#include "singleton/CComponentFactory.hpp"
#include "WalletCore/PrivateKey.h"
#include <boost/algorithm/hex.hpp>
#include <crypto/hasher/hasher_impl.hpp>
#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>

namespace
{
    std::array<uint8_t, 32> get_elgamal_pubkey()
    {
        const auto              elgamal_key = KeyGenerator::ElGamal( 0x1234_cppui256 ).GetPublicKey().public_key_value;
        std::array<uint8_t, 32> exported;
        export_bits( elgamal_key, exported.begin(), 8, false );

        return exported;
    }
}

namespace sgns
{
    const std::array<uint8_t, 32> GeniusAccount::ELGAMAL_PUBKEY_PREDEFINED = get_elgamal_pubkey();

    GeniusAccount::GeniusAccount( TokenID token_id, std::string_view base_path, const char *eth_private_key ) :
        token( token_id ), nonce( 0 )
    {
        if ( auto maybe_address = GenerateGeniusAddress( base_path, eth_private_key ); maybe_address.has_value() )
        {
            auto [temp_elgamal_address, temp_eth_address] = maybe_address.value();

            eth_keypair     = std::make_shared<ethereum::EthereumKeyGenerator>( std::move( temp_eth_address ) );
            elgamal_address = std::make_shared<KeyGenerator::ElGamal>( std::move( temp_elgamal_address ) );
        }
        else
        {
            std::cerr << "Could not get SGNS address: " << maybe_address.error().message() << std::endl;
            throw std::runtime_error( maybe_address.error().message() );
        }

        //TODO - Retrieve values where? Read through blockchain Here?
        // How to deal with errors?
    }

    GeniusAccount::~GeniusAccount()
    {
        utxos.clear();
    }

    std::string GeniusAccount::GetAddress() const
    {
        return eth_keypair->GetEntirePubValue();
    }

    template <>
    uint64_t GeniusAccount::GetBalance() const
    {
        uint64_t retval = 0;

        for ( auto &curr : utxos )
        {
            if ( !curr.GetLock() )
            {
                retval += curr.GetAmount();
            }
        }

        return retval;
    }

    template <>
    std::string GeniusAccount::GetBalance() const
    {
        return std::to_string( GetBalance<uint64_t>() );
    }

    TokenID GeniusAccount::GetToken() const
    {
        return token;
    }

    bool GeniusAccount::PutUTXO( const GeniusUTXO &new_utxo )
    {
        bool is_new = true;
        for ( auto &curr : utxos )
        {
            if ( new_utxo.GetTxID() != curr.GetTxID() )
            {
                continue;
            }
            if ( new_utxo.GetOutputIdx() != curr.GetOutputIdx() )
            {
                continue;
            }
            //TODO - If it's the same, might be locked, then unlock
            is_new = false;
            break;
        }
        if ( is_new )
        {
            utxos.push_back( new_utxo );
        }
        return is_new;
    }

    bool GeniusAccount::RefreshUTXOs( const std::vector<InputUTXOInfo> &infos )
    {
        utxos.erase( std::remove_if( utxos.begin(),
                                         utxos.end(),
                                         [&infos]( const GeniusUTXO &x ) { //
                                             return std::any_of( infos.begin(),
                                                                 infos.end(),
                                                                 [&x]( const InputUTXOInfo &a ) { //
                                                                     return ( a.txid_hash_ == x.GetTxID() ) &&
                                                                            ( a.output_idx_ == x.GetOutputIdx() );
                                                                 } );
                                         } ),
                         utxos.end() );
        return true;
    }

    bool GeniusAccount::VerifySignature( std::string address, std::string sig, std::vector<uint8_t> data )
    {
        bool         ret                = false;
        const size_t SIGNATURE_EXP_SIZE = 64;
        do
        {
            if ( sig.size() != SIGNATURE_EXP_SIZE )
            {
                std::cout << "NO CORRECT SIZE" << std::endl;
                break;
            }
            std::vector<uint8_t> vec_sig( sig.cbegin(), sig.cend() );

            std::array<uint8_t, 32> hashed = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( data );

            auto [r_success, r] =
                nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_from_bytes(
                    vec_sig.cbegin(),
                    vec_sig.cbegin() + 32 );

            if ( !r_success )
            {
                break;
            }
            auto [s_success, s] =
                nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_from_bytes(
                    vec_sig.cbegin() + 32,
                    vec_sig.cbegin() + 64 );

            if ( !s_success )
            {
                break;
            }
            ethereum::signature_type sig( r, s );
            auto                     eth_pubkey = ethereum::EthereumKeyGenerator::BuildPublicKey( address );
            ret                                 = nil::crypto3::verify( hashed, sig, eth_pubkey );
        } while ( 0 );

        return ret;
    }

    std::vector<uint8_t> GeniusAccount::Sign( std::vector<uint8_t> data )
    {
        std::array<uint8_t, 32> hashed = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( data );

        ethereum::signature_type  signature = nil::crypto3::sign( hashed, eth_keypair->get_private_key() );
        std::vector<std::uint8_t> signed_vector( 64 );

        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( std::get<0>( signature ),
                                                  signed_vector.begin(),
                                                  signed_vector.begin() + 32 );
        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( std::get<1>( signature ),
                                                  signed_vector.begin() + 32,
                                                  signed_vector.end() );

        return signed_vector;
    }

    outcome::result<std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>> GeniusAccount::
        GenerateGeniusAddress( std::string_view base_path, const char *eth_private_key )
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
            if ( eth_private_key == nullptr )
            {
                return outcome::failure( std::errc::invalid_argument );
            }

            OUTCOME_TRY( auto as_vec, base::unhex( eth_private_key ) );

            TW::PrivateKey private_key( as_vec );

            auto signed_secret = private_key.sign(
                TW::Data( ELGAMAL_PUBKEY_PREDEFINED.cbegin(), ELGAMAL_PUBKEY_PREDEFINED.cend() ),
                TWCurveSECP256k1 );

            if ( signed_secret.empty() )
            {
                std::cerr << "Could not sign secret\n";
                return outcome::failure( std::errc::invalid_argument );
            }

            auto hashed = TW::Hash::sha256( signed_secret );

            key_seed = nil::crypto3::multiprecision::uint256_t( hashed );

            BOOST_OUTCOME_TRYV2( auto &&,
                                 secure_storage->Save( "sgns_key", key_seed.str(), std::string( base_path ) ) );
        }

        KeyGenerator::ElGamal          elgamal_key( key_seed );
        ethereum::EthereumKeyGenerator eth_key( key_seed );

        return std::make_pair( std::move( elgamal_key ), std::move( eth_key ) );
    }

    uint64_t GeniusAccount::GetBalance( const TokenID token_id ) const
    {
        uint64_t balance = 0;
        for ( const auto &utxo : utxos )
        {
            if ( !utxo.GetLock() && token_id.Equals( utxo.GetTokenID() ) )
            {
                balance += utxo.GetAmount();
            }
        }
        return balance;
    }

}
