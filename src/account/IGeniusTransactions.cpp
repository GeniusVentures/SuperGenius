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

    std::vector<uint8_t> IGeniusTransactions::MakeSignature( std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key )
    {
        dag_st.clear_signature();
        auto                 size = dag_st.ByteSizeLong();
        std::vector<uint8_t> serialized( size );
        dag_st.SerializeToArray( serialized.data(), size );

        std::array<uint8_t, 32> hashed = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( serialized );

        ethereum::signature_type  signature = nil::crypto3::sign( hashed, eth_key->get_private_key() );
        std::vector<std::uint8_t> signed_vector( 64 );

        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( std::get<0>( signature ),
                                                  signed_vector.begin(),
                                                  signed_vector.begin() + 32 );
        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( std::get<1>( signature ),
                                                  signed_vector.begin() + 32,
                                                  signed_vector.end() );

        nil::crypto3::multiprecision::cpp_int r;
        nil::crypto3::multiprecision::cpp_int s;

        import_bits( r, signed_vector.cbegin(), signed_vector.cbegin() + 32 );
        import_bits( s, signed_vector.cbegin() + 32, signed_vector.cbegin() + 64 );

        dag_st.set_signature( signed_vector.data(), signed_vector.size() );
        return signed_vector;
    }

    bool IGeniusTransactions::CheckDAGStructSignature( SGTransaction::DAGStruct dag_st )
    {
        bool         ret                = false;
        auto         str_signature      = dag_st.signature();
        const size_t SIGNATURE_EXP_SIZE = 64;
        do
        {
            if ( str_signature.size() != SIGNATURE_EXP_SIZE )
            {
                break;
            }
            std::vector<uint8_t> vec_sig( str_signature.cbegin(), str_signature.cend() );

            dag_st.clear_signature();
            auto                 size = dag_st.ByteSizeLong();
            std::vector<uint8_t> serialized( size );
            dag_st.SerializeToArray( serialized.data(), size );

            std::array<uint8_t, 32> hashed = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( serialized );

            auto [r_success, r] =
                nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_from_bytes(
                    vec_sig.cbegin(),
                    vec_sig.cbegin() + 32 );

            if ( !r_success )
            {
                break;
            }
            auto [s_success, s] =
                nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_from_bytes(
                    vec_sig.cbegin() + 32,
                    vec_sig.cbegin() + 64 );

            if ( !s_success )
            {
                break;
            }
            ethereum::signature_type sig( r, s );
            auto eth_pubkey = ethereum::EthereumKeyGenerator::BuildPublicKey( dag_st.source_addr() );
            ret             = nil::crypto3::verify( hashed, sig, eth_pubkey );
        } while ( 0 );

        return ret;
    }
}
