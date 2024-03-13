/**
 * @file       AccountManager.hpp
 * @brief      
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _ACCOUNT_MANAGER_HPP_
#define _ACCOUNT_MANAGER_HPP_
#include "integration/IComponent.hpp"
#include "account/GeniusAccount.hpp"
#include <boost/multiprecision/cpp_int.hpp>

using namespace boost::multiprecision;
namespace sgns
{
    class AccountManager : public IComponent
    {
        public:
        boost::optional<GeniusAccount> CreateAccount(const std::string &priv_key_data, const uint64_t &initial_amount)
        {
            uint256_t value_in_num(priv_key_data);
            return GeniusAccount(value_in_num,initial_amount,0);
        }
        //void ImportAccount(const std::string &priv_key_data);
        
        std::string GetName() override
        {
            return "AccountManager";
        }
    };
};

#endif
