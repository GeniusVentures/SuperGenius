#include "IGeniusTransactions.hpp"

namespace sgns
{
    outcome::result<SGTransaction::DAGStruct> IGeniusTransactions::DeSerializeDAGStruct( std::vector<uint8_t> &data )
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
}
