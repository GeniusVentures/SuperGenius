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
            //TODO - Retrieve values where? on Transaction manager?
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
        const std::string GetBalance() const
        {
            return std::to_string( balance );
        }
        const std::string GetToken() const
        {
            return "GNUS Token";
        }
        const std::string GetNonce() const
        {
            return std::to_string( nonce );
        }

        uint256_t               address;
        uint64_t                balance;
        uint8_t                 token; //GNUS SGNUS ETC...
        uint64_t                nonce;
        std::vector<GeniusUTXO> utxos;

    private:
    };
}

#endif