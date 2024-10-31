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
#include <boost/multiprecision/fwd.hpp>

#include "ProofSystem/ElGamalKeyGenerator.hpp"
#include "account/GeniusUTXO.hpp"
#include "account/UTXOTxParameters.hpp"
#include <KDFGenerator/KDFGenerator.hpp>
#include "outcome/outcome.hpp"

namespace sgns
{
    using namespace boost::multiprecision;

    class GeniusAccount
    {
    public:
        GeniusAccount( const uint8_t token_type, const std::string &base_path, std::string_view eth_private_key ) :
            token( token_type ), //
            nonce( 0 ),          //
            balance( 0 )         //
        {
            auto maybe_address = GenerateGeniusAddress( base_path, eth_private_key );

            if ( maybe_address )
            {
                address = maybe_address.value();
            }
            //TODO - Retrieve values where? Read through blockchain Here?
            // How to deal with errors?
        }

        ~GeniusAccount()
        {
            utxos.clear();
            std::cout << "Delete account" << std::endl;
        }

        template <typename T>
        T GetAddress() const;

        template <>
        [[nodiscard]] std::string GetAddress() const
        {
            std::ostringstream oss;
            oss << "0x";
            oss << std::hex << address.GetPublicKey().public_key_value;

            return oss.str();
        }

        template <>
        [[nodiscard]] boost::multiprecision::uint256_t GetAddress() const
        {
            return boost::multiprecision::uint256_t( address.GetPublicKey().public_key_value.str() );
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
                if ( !curr.GetLock() )
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

        KeyGenerator::ElGamal   address;
        uint8_t                 token; //GNUS SGNUS ETC...
        uint64_t                nonce;
        std::vector<GeniusUTXO> utxos;

    private:
        uint64_t balance;

        static outcome::result<KeyGenerator::ElGamal> GenerateGeniusAddress( std::string_view base_path,
                                                                             std::string_view eth_private_key );
    };
}

#endif
