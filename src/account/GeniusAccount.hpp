/**
 * @file       GeniusAccount.hpp
 * @brief      
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _GENIUS_ACCOUNT_HPP_
#define _GENIUS_ACCOUNT_HPP_
#include <string>
#include <cstdint>
#include <boost/multiprecision/cpp_int.hpp>
#include "account/GeniusUTXO.hpp"
#include "account/TransferTransaction.hpp"
#include "singleton/CComponentFactory.hpp"
#include "local_secure_storage/ISecureStorage.hpp"
#include <KDFGenerator/KDFGenerator.hpp>
#include "outcome/outcome.hpp"
#include "base/util.hpp"

using namespace boost::multiprecision;

namespace sgns
{
    class GeniusAccount
    {
    public:
        GeniusAccount( const uint8_t token_type, const std::string &base_path ) :
            token( token_type ), //
            nonce( 0 ),          //
            balance( 0 )         //
        {
            auto maybe_address = GenerateGeniusAddress( base_path );

            if ( maybe_address )
            {
                address = maybe_address.value();
            }
            //TODO - Retrieve values where? Read through blockchain Here?
        }

        ~GeniusAccount()
        {
            utxos.clear();
            std::cout << "Delete account" << std::endl;
        }

        template <typename T>
        const T GetAddress() const;

        template <>
        [[nodiscard]] const std::string GetAddress() const
        {
            std::ostringstream oss;
            oss << std::hex << address;

            return "0x" + oss.str();
        }

        template <>
        [[nodiscard]] const uint256_t GetAddress() const
        {
            return address;
        }

        template <typename T>
        [[nodiscard]] T GetBalance() const;

        template <>
        [[nodiscard]] uint64_t GetBalance() const
        {
            uint64_t retval = 0;

            for ( auto &curr : utxos )
            {
                std::cout << "utxo's ID: " << curr.GetTxID() << std::endl;
                std::cout << "utxo's Amount: " << curr.GetAmount() << std::endl;
                std::cout << "utxo's GetOutputIdx: " << curr.GetOutputIdx() << std::endl;
                std::cout << "utxo's GetLock: " << curr.GetLock() << std::endl;
                if ( curr.GetLock() == false )
                {
                    retval += curr.GetAmount();
                }
            }
            return retval;
        }

        template <>
        [[nodiscard]] std::string GetBalance() const
        {
            return std::to_string( GetBalance<uint64_t>() );
        }

        [[nodiscard]] std::string GetToken() const
        {
            return "GNUS Token";
        }

        [[nodiscard]] std::string GetNonce() const
        {
            return std::to_string( nonce );
        }

        bool PutUTXO( const GeniusUTXO &new_utxo )
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

        bool RefreshUTXOs( const std::vector<InputUTXOInfo> &infos )
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

        uint256_t               address;
        uint8_t                 token; //GNUS SGNUS ETC...
        uint64_t                nonce;
        std::vector<GeniusUTXO> utxos;

    private:
        uint64_t balance;

        outcome::result<uint256_t> GenerateGeniusAddress( const std::string &base_path )
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );
            OUTCOME_TRY( ( auto &&, icomponent ), component_factory->GetComponent( "LocalSecureStorage" ) );

            auto secure_storage = std::dynamic_pointer_cast<ISecureStorage>( icomponent );
            auto load_res       = secure_storage->Load( "sgns_key", base_path );

            std::vector<uint8_t> key_seed;
            if ( !load_res )
            {
                //Create file/key
                auto               random_number = GenerateRandomNumber();
                std::ostringstream oss;
                oss << std::hex << random_number;

                std::string rnd_string = "0x" + oss.str();
                BOOST_OUTCOME_TRYV2( auto &&, secure_storage->Save( "sgns_key", rnd_string, base_path ) );
                key_seed = HexASCII2NumStr<std::uint8_t>( oss.str().data(), oss.str().size() );
            }
            else
            {
                key_seed = HexASCII2NumStr<std::uint8_t>( &load_res.value().data()[2], load_res.value().size() - 2 );
            }
            auto result = KDFGenerator::GenerateKDF( key_seed, KDFGenerator::HashType::SHA256 );

            return Vector2Num<uint256_t>( result );
        }
    };
}

#endif
