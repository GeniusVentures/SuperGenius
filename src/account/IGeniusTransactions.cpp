#include "IGeniusTransactions.hpp"

#include <crypto/hasher/hasher_impl.hpp>

namespace sgns
{
    outcome::result<SGTransaction::DAGStruct> IGeniusTransactions::DeSerializeDAGStruct(
        const std::vector<uint8_t> &data )
    {
        SGTransaction::DAGWrapper dag_wrap;
        if ( !dag_wrap.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse DAGStruct from array.\n";
            return outcome::failure( boost::system::error_code{} );
        }
        SGTransaction::DAGStruct dag;
        dag.CopyFrom( *dag_wrap.mutable_dag_struct() );
        return dag;
    }

    outcome::result<SGTransaction::DAGStruct> IGeniusTransactions::DeSerializeDAGStruct( const std::string &data )
    {
        SGTransaction::DAGWrapper dag_wrap;
        if ( !dag_wrap.ParseFromString( data ) )
        {
            std::cerr << "Failed to parse DAGStruct from array.\n";
            return outcome::failure( boost::system::error_code{} );
        }
        SGTransaction::DAGStruct dag;
        dag.CopyFrom( *dag_wrap.mutable_dag_struct() );
        return dag;
    }

    void IGeniusTransactions::FillHash()
    {
        auto signature = dag_st.signature();
        dag_st.clear_signature();
        dag_st.clear_data_hash();

        auto hasher_ = std::make_shared<crypto::HasherImpl>();
        auto hash    = hasher_->blake2b_256( SerializeByteVector() );

        dag_st.set_data_hash( hash.toReadableString() );
        dag_st.set_signature( std::move( signature ) );
    }
}
