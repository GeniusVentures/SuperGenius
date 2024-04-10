/**
 * @file       IGeniusTransactions.hpp
 * @brief      Transaction interface class
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _IGENIUS_TRANSACTIONS_HPP_
#define _IGENIUS_TRANSACTIONS_HPP_

#include <vector>
#include <string>
#include <boost/optional.hpp>

namespace sgns
{
    //class GeniusBlockHeader; //TODO - Design new header or rework old one

    class IGeniusTransactions
    {
    public:
        IGeniusTransactions( const std::string &type ) : transaction_type( type )
        {
        }
        virtual ~IGeniusTransactions() = default;
        virtual const std::string GetType() const
        {
            return transaction_type;
        }

        virtual std::vector<uint8_t> SerializeByteVector() = 0;

    private:
        const std::string transaction_type;
    };
}

#endif
