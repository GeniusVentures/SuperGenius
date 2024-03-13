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

using namespace boost::multiprecision;
namespace sgns
{
    class GeniusAccount
    {
    public:
        GeniusAccount( const uint256_t &addr, const uint64_t &initial_balance, const uint8_t token_type ) :
            address( addr ),            //
            balance( initial_balance ), //
            token( token_type )         //
        {
        }

        uint256_t address;
        uint64_t  balance;
        uint8_t   token; //GNUS SGNUS ETC...
    private:
    };
}

#endif