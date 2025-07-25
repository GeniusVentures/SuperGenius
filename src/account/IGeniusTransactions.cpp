#include "IGeniusTransactions.hpp"

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

    bool IGeniusTransactions::CheckHash()
    {
        auto signature = dag_st.signature();
        auto hash      = dag_st.data_hash();
        dag_st.clear_signature();
        dag_st.clear_data_hash();

        auto hasher_         = std::make_shared<crypto::HasherImpl>();
        auto calculated_hash = hasher_->blake2b_256( SerializeByteVector() );
        dag_st.set_data_hash(  hash );
        dag_st.set_signature( std::move( signature ) );

        return ( hash == calculated_hash.toReadableString() );
    }

    std::vector<uint8_t> IGeniusTransactions::MakeSignature( std::shared_ptr<GeniusAccount> account )
    {
        dag_st.clear_signature();
        auto serialized = SerializeByteVector();

        std::vector<std::uint8_t> signed_vector( 64 );

        signed_vector = account->Sign( serialized );

        dag_st.set_signature( signed_vector.data(), signed_vector.size() );
        return signed_vector;
    }

    bool IGeniusTransactions::CheckSignature()
    {
        auto str_signature = dag_st.signature();
        dag_st.clear_signature();
        auto serialized = SerializeByteVector();
        dag_st.set_signature( str_signature );

        return GeniusAccount::VerifySignature( dag_st.source_addr(), str_signature, serialized );
    }

    bool IGeniusTransactions::CheckDAGSignatureLegacy()
    {
        auto str_signature = dag_st.signature();
        dag_st.clear_signature();
        auto                 size = dag_st.ByteSizeLong();
        std::vector<uint8_t> serialized( size );
        dag_st.SerializeToArray( serialized.data(), size );
        dag_st.set_signature( str_signature );

        return GeniusAccount::VerifySignature( dag_st.source_addr(), str_signature, serialized ) && CheckHash();
    }
}
