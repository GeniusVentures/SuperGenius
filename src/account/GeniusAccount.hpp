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

#include <ProofSystem/ElGamalKeyGenerator.hpp>
#include <ProofSystem/EthereumKeyGenerator.hpp>

#include "account/GeniusUTXO.hpp"
#include "account/UTXOTxParameters.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{
    using namespace boost::multiprecision;

    class GeniusAccount
    {
    public:
        static const std::array<uint8_t, 32> ELGAMAL_PUBKEY_PREDEFINED;

        GeniusAccount( std::string token_id, std::string_view base_path, const char *eth_private_key );

        ~GeniusAccount()
        {
            utxos.clear();
        }

        [[nodiscard]] std::string GetAddress() const
        {
            return eth_address->GetEntirePubValue();
        }

        //template <>
        //[[nodiscard]] uint256_t GetAddress() const
        //{
        //    return uint256_t( eth_address.GetPublicKey().public_key_value.str() );
        //}

        template <typename T>
        [[nodiscard]] T GetBalance() const;

        template <>
        [[nodiscard]] uint64_t GetBalance() const
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
        [[nodiscard]] std::string GetBalance() const
        {
            return std::to_string( GetBalance<uint64_t>() );
        }

        [[nodiscard]] std::string GetToken() const
        {
            return token;
            // return "GNUS Token";
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

        std::shared_ptr<KeyGenerator::ElGamal>          elgamal_address;
        std::shared_ptr<ethereum::EthereumKeyGenerator> eth_address;

        std::string             token; //GNUS SGNUS ETC...
        uint64_t                nonce;
        std::vector<GeniusUTXO> utxos;

    private:
        //uint64_t balance;

        static outcome::result<std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>> GenerateGeniusAddress(
            std::string_view base_path,
            const char      *eth_private_key );
    };
}

#endif
