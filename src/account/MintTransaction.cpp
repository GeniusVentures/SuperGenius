/**
 * @file       MintTransaction.cpp
 * @brief      
 * @date       2024-04-10
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/MintTransaction.hpp"

namespace sgns
{
    MintTransaction::MintTransaction( const uint64_t &new_amount, const SGTransaction::DAGStruct &dag ) :
        IGeniusTransactions( "mint", SetDAGWithType( dag, "mint" ) ), //
        amount( new_amount )                                          //
    {
    }
    std::vector<uint8_t> MintTransaction::SerializeByteVector()
    {
        SGTransaction::MintTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( this->dag_st );
        tx_struct.set_amount( amount );
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }
    MintTransaction MintTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {

        SGTransaction::MintTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse TransferTx from array." << std::endl;
        }
        uint64_t v64 = tx_struct.amount();
        //std::memcpy( &v64, &( *data.begin() ), sizeof( v64 ) );

        return MintTransaction( v64, tx_struct.dag_struct() ); // Return new instance
    }
    const uint64_t MintTransaction::GetAmount() const
    {
        return amount;
    }
}