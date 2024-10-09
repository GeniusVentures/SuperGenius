/**
 * @file       IGeniusTransactions.hpp
 * @brief      Transaction interface class
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _IGENIUS_TRANSACTIONS_HPP_
#define _IGENIUS_TRANSACTIONS_HPP_

#include <utility>
#include <vector>
#include <string>

#include <boost/optional.hpp>
#include <boost/format.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include "account/proto/SGTransaction.pb.h"

namespace sgns
{
    using namespace boost::multiprecision;

    //class GeniusBlockHeader; //TODO - Design new header or rework old one

    class IGeniusTransactions
    {
    public:
        IGeniusTransactions( std::string type, SGTransaction::DAGStruct dag ) :
            dag_st( std::move( dag ) ), transaction_type( std::move( type ) )
        {
        }

        virtual ~IGeniusTransactions() = default;

        [[nodiscard]] std::string GetType() const
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

        virtual std::string GetTransactionSpecificPath() = 0;

        std::string GetTransactionFullPath()
        {
            boost::format full_path( GetSrcAddress<std::string>() + "/tx/" + GetTransactionSpecificPath() + "/%llu" );
            full_path % dag_st.nonce();

            return full_path.str();
        }
        std::string GetProofFullPath()
        {
        boost::format full_path( GetSrcAddress<std::string>() + "/tx/" + "proof" + "/%llu" );
            full_path % dag_st.nonce();

            return full_path.str();
        }

        template <typename T>
        T GetSrcAddress() const;

        template <>
        std::string GetSrcAddress<std::string>() const
        {
            //std::string address(bytes_data.begin(), bytes_data.end());
            //std::ostringstream oss;
            //oss << std::hex << src_address;

            return dag_st.source_addr();
        }

        template <>
        uint256_t GetSrcAddress<uint256_t>() const
        {
            return uint256_t{ dag_st.source_addr() };
        }

        SGTransaction::DAGStruct dag_st;

    private:
        const std::string transaction_type;
    };
}

#endif
