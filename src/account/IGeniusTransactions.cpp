#include "IGeniusTransactions.hpp"
#include "GeniusAccount.hpp"
#include <crypto/hasher/hasher_impl.hpp>
#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>

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

    std::vector<uint8_t> IGeniusTransactions::MakeSignature( std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key )
    {
        dag_st.clear_signature();
        auto                 size = dag_st.ByteSizeLong();
        std::vector<uint8_t> serialized( size );
        dag_st.SerializeToArray( serialized.data(), size );

        std::vector<std::uint8_t> signed_vector( 64 );

        signed_vector = GeniusAccount::Sign( eth_key, serialized );

        dag_st.set_signature( signed_vector.data(), signed_vector.size() );
        return signed_vector;
    }

    bool IGeniusTransactions::CheckDAGStructSignature( SGTransaction::DAGStruct dag_st )
    {
        auto str_signature = dag_st.signature();
        dag_st.clear_signature();
        auto                 size = dag_st.ByteSizeLong();
        std::vector<uint8_t> serialized( size );
        dag_st.SerializeToArray( serialized.data(), size );

        return GeniusAccount::VerifySignature( dag_st.source_addr(), str_signature, serialized );
    }
}
