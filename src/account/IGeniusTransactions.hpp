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
#include <boost/multiprecision/cpp_int.hpp>

using namespace boost::multiprecision;

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
            SGTransaction::DAGWrapper dag_wrap;
            if ( !dag_wrap.ParseFromArray( data.data(), data.size() ) )
            {
                std::cerr << "Failed to parse DAGStruct from array." << std::endl;
                return boost::none;
            }
            SGTransaction::DAGStruct dag;
            dag.CopyFrom( *dag_wrap.mutable_dag_struct() );
            return dag;
        }

        static SGTransaction::DAGStruct SetDAGWithType( const SGTransaction::DAGStruct &dag, const std::string &type )
        {
            SGTransaction::DAGStruct dag_with_type = dag;
            dag_with_type.set_type( type );
            return dag_with_type;
        }

        virtual std::vector<uint8_t> SerializeByteVector() = 0;

        template <typename T>
        const T GetSrcAddress() const;

        template <>
        const std::string GetSrcAddress<std::string>() const
        {

            //std::string address(bytes_data.begin(), bytes_data.end());
            //std::ostringstream oss;
            //oss << std::hex << src_address;

            return dag_st.source_addr();
        }
        template <>
        const uint256_t GetSrcAddress<uint256_t>() const
        {
            return uint256_t{dag_st.source_addr()};
        }

    protected:
        SGTransaction::DAGStruct dag_st;

    private:
        const std::string transaction_type;
    };
}

#endif
