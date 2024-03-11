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
    class GeniusBlockHeader; //TODO - Design new header or rework old one

    class IGeniusTransactions
    {
    public:
        virtual ~IGeniusTransactions()            = default;
        virtual const std::string GetType() const = 0;

        boost::optional<GeniusBlockHeader> InitHeader()
        {
            //return GeniusBlockHeader();
        }

    private:
        //GeniusBlockHeader block_header;
    };
}

#endif
