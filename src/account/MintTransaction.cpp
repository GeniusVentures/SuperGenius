/**
 * @file       MintTransaction.cpp
 * @brief      
 * @date       2024-04-10
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/MintTransaction.hpp"
#include "crypto/hasher/hasher_impl.hpp"

namespace sgns
{
    MintTransaction::MintTransaction( uint64_t                        new_amount,
                                      std::string                     chainid,
                                      std::string                     tokenid,
                                      const SGTransaction::DAGStruct &dag ) :
        IGeniusTransactions( "mint", SetDAGWithType( dag, "mint" ) ), //
        amount( new_amount ),                                         //
        chain_id( chainid ),                                          //
        token_id( tokenid )
    {
        auto hasher_ = std::make_shared<sgns::crypto::HasherImpl>();
        auto hash    = hasher_->blake2b_256( SerializeByteVector() );
        dag_st.set_data_hash( hash.toReadableString() );
    }

    std::vector<uint8_t> MintTransaction::SerializeByteVector()
    {
        SGTransaction::MintTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( this->dag_st );
        tx_struct.set_amount( amount );
        tx_struct.set_chain_id( chain_id );
        tx_struct.set_token_id( token_id );
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }

    std::shared_ptr<MintTransaction> MintTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        SGTransaction::MintTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse TransferTx from array." << std::endl;
        }
        uint64_t    v64     = tx_struct.amount();
        std::string chainid = tx_struct.chain_id();
        std::string tokenid = tx_struct.token_id();
        //std::memcpy( &v64, &( *data.begin() ), sizeof( v64 ) );

        return std::make_shared<MintTransaction>( v64,
                                                  chainid,
                                                  tokenid,
                                                  tx_struct.dag_struct() ); // Return new instance
    }

    uint64_t MintTransaction::GetAmount() const
    {
        return amount;
    }
}
