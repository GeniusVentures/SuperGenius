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
#include "account/proto/SGTransaction.pb.h"

namespace sgns
{
    //class GeniusBlockHeader; //TODO - Design new header or rework old one

    class IGeniusTransactions
    {
    public:
        IGeniusTransactions( const std::string &type, const SGTransaction::DAGStruct &dag ) :
            transaction_type( type ), //
            dag_st( dag )             //
        {
        }
        virtual ~IGeniusTransactions() = default;
        const std::string GetType() const
        {
            return transaction_type;
        }

        static boost::optional<SGTransaction::DAGStruct> DeSerializeDAGStruct( std::vector<uint8_t> &data )
        {
            SGTransaction::DAGStruct dag;
            if ( !dag.ParseFromArray( data.data(), data.size() ) )
            {
                std::cerr << "Failed to parse DAGStruct from array." << std::endl;
                return boost::none; 
            }
            return dag;
        }

        static SGTransaction::DAGStruct SetDAGWithType( const SGTransaction::DAGStruct &dag, const std::string &type )
        {
            SGTransaction::DAGStruct dag_with_type = dag;
            dag_with_type.set_type( type );
            return dag_with_type;
        }

        virtual std::vector<uint8_t> SerializeByteVector() = 0;

    private:
        const std::string transaction_type;

        SGTransaction::DAGStruct dag_st;
    };
}

#endif
