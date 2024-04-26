/**
 * @file       GeniusAccount.hpp
 * @brief      
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _GENIUS_ACCOUNT_HPP_
#define _GENIUS_ACCOUNT_HPP_
#include <string>
#include <vector>
#include <cstdint>
#include <boost/multiprecision/cpp_int.hpp>
#include "account/GeniusUTXO.hpp"
#include "account/TransferTransaction.hpp"

using namespace boost::multiprecision;
namespace sgns
{
    class GeniusAccount
    {
    public:
        GeniusAccount( const uint256_t &addr, const uint64_t &initial_balance, const uint8_t token_type ) :
            address( addr ),            //
            balance( initial_balance ), //
            token( token_type ),        //
            nonce( 0 )
        {
            std::cout << "Genius account" << std::endl;
            //TODO - Retrieve values where? on Transaction manager?
        }

        ~GeniusAccount()
        {
            utxos.clear();
            std::cout << "Delete account" << std::endl;
        }

        template <typename T>
        const T GetAddress() const;

        template <>
        const std::string GetAddress() const
        {
            std::ostringstream oss;
            oss << std::hex << address;

            return ( "0x" + oss.str() );
        }
        template <>
        const uint256_t GetAddress() const
        {
            return address;
        }

        template <typename T>
        const T GetBalance() const;

        template <>
        const uint64_t GetBalance() const
        {
            uint64_t retval = 0;

            for ( auto &curr : utxos )
            {
                std::cout << "my utxo's ID: " << curr.GetTxID() << std::endl;
                std::cout << "my utxo's Amount: " << curr.GetAmount() << std::endl;
                std::cout << "my utxo's GetOutputIdx: " << curr.GetOutputIdx() << std::endl;
                std::cout << "my utxo's GetLock: " << curr.GetLock() << std::endl;
                if ( curr.GetLock() == false )
                {
                    retval += curr.GetAmount();
                }
            }
            return retval;
        }
        template <>
        const std::string GetBalance() const
        {
            return std::to_string( GetBalance<uint64_t>() );
        }
        const std::string GetToken() const
        {
            return "GNUS Token";
        }
        const std::string GetNonce() const
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

        bool RefreshUTXOs( const std::vector<TransferTransaction::InputUTXOInfo> &infos )
        {
            std::cout << "Let's refresh the ones we spent" << std::endl;
            utxos.erase( std::remove_if( utxos.begin(), utxos.end(),
                                         [&infos]( const GeniusUTXO &x ) { //
                                             return std::any_of( infos.begin(), infos.end(),
                                                                 [&x]( const TransferTransaction::InputUTXOInfo &a ) { //
                                                                     return ( ( a.txid_hash_ == x.GetTxID() ) &&
                                                                              ( a.output_idx_ == x.GetOutputIdx() ) && ( x.GetLock() == true ) );
                                                                 } );
                                         } ),
                         utxos.end() );
            /*for ( auto &info : infos )
            {
                for ( std::size_t i = 0; i < utxos.size(); ++i )
                {
                    if ( utxos[i].txid_hash_ != info.txid_hash_ )
                    {
                        continue;
                    }
                    if ( utxos[i].output_idx_ != info.output_idx_ )
                    {
                        continue;
                    }
                    if ( utxos[i].locked_ == true )
                    {
                        utxos[i].locked_ = false;
                    }
                }
            }*/
            return true;
        }

        std::pair<std::vector<TransferTransaction::InputUTXOInfo>, uint64_t> GetInputsFromUTXO( const uint64_t &amount )
        {
            std::vector<TransferTransaction::InputUTXOInfo> retval;

            auto temp_utxos = utxos; //so we don't change anything unless success;

            int64_t remain = static_cast<int64_t>( amount );

            for ( auto &utxo : temp_utxos )
            {
                if ( remain <= 0 )
                {
                    break;
                }
                //TODO - Insert signature
                TransferTransaction::InputUTXOInfo curr_input{ utxo.GetTxID(), utxo.GetOutputIdx(), "" };
                remain -= utxo.GetAmount();
                utxo.ToggleLock( true );
                retval.push_back( curr_input );
            }

            if ( remain <= 0 )
            {
                utxos = temp_utxos;
            }
            else
            {
                retval.clear();
                remain = 0;
            }
            return std::make_pair( retval, static_cast<uint64_t>( std::abs( remain ) ) );
        }

        uint256_t               address;
        uint8_t                 token; //GNUS SGNUS ETC...
        uint64_t                nonce;
        std::vector<GeniusUTXO> utxos;

    private:
        uint64_t balance;
    };
}

#endif