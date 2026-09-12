#include "GeniusTransaction.hpp"

#include "crypto/hasher.hpp"

namespace sgns
{
    outcome::result<SGTransaction::DAGStruct> GeniusTransaction::DeSerializeDAGStruct(
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

    outcome::result<SGTransaction::DAGStruct> GeniusTransaction::DeSerializeDAGStruct( const std::string &data )
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

    void GeniusTransaction::FillHash()
    {
        auto signature = dag_st.signature();
        dag_st.clear_signature();
        dag_st.clear_data_hash();

        auto hash = crypto::blake2b_256( SerializeByteVector() );

        dag_st.set_data_hash( hash.toReadableString() );
        dag_st.set_signature( std::move( signature ) );
    }

    bool GeniusTransaction::CheckHash() const
    {
        const auto hash = dag_st.data_hash();

        SGTransaction::DAGStruct dag_copy = dag_st;
        dag_copy.clear_signature();
        dag_copy.clear_data_hash();

        auto calculated_hash = crypto::blake2b_256( SerializeByteVector( dag_copy ) );

        return hash == calculated_hash.toReadableString();
    }

    std::vector<uint8_t> GeniusTransaction::MakeSignature( GeniusAccount &account )
    {
        dag_st.clear_signature();
        auto serialized = SerializeByteVector();

        std::vector<std::uint8_t> signed_vector( 64 );

        signed_vector = account.Sign( serialized );

        dag_st.set_signature( signed_vector.data(), signed_vector.size() );
        return signed_vector;
    }

    bool GeniusTransaction::CheckSignature() const
    {
        return CheckSignatureAgainst( dag_st.source_addr() );
    }

    bool GeniusTransaction::CheckSignatureAgainst( const std::string &address ) const
    {
        auto       str_signature = dag_st.signature();

        SGTransaction::DAGStruct dag_copy = dag_st;
        dag_copy.clear_signature();
        auto serialized = SerializeByteVector(dag_copy);

        return GeniusAccount::VerifySignature( address, str_signature, serialized );
    }

    bool GeniusTransaction::CheckDAGSignatureLegacy() const
    {
        auto str_signature = dag_st.signature();
                SGTransaction::DAGStruct dag_copy = dag_st;
        dag_copy.clear_signature();
        auto                 size = dag_copy.ByteSizeLong();
        std::vector<uint8_t> serialized( size );
        if ( !dag_copy.SerializeToArray( serialized.data(), size ) )
        {
            std::cerr << "Failed to serialize DAG struct\n";
        }

        return GeniusAccount::VerifySignature( dag_st.source_addr(), str_signature, serialized ) && CheckHash();
    }

    std::string GeniusTransaction::GetHash() const
    {
        return dag_st.data_hash();
    }

    std::string GeniusTransaction::GetPreviousHash() const
    {
        return dag_st.previous_hash();
    }

    std::string GeniusTransaction::GetUncleHash() const
    {
        return dag_st.uncle_hash();
    }

    std::unordered_set<std::string> GeniusTransaction::GetTopics() const
    {
        return { GetSrcAddress() };
    }
}
