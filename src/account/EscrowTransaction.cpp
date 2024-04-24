/**
 * @file       EscrowTransaction.cpp
 * @brief      
 * @date       2024-04-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/EscrowTransaction.hpp"

namespace sgns
{
    EscrowTransaction::EscrowTransaction( const uint64_t &hold_amount, const std::string &job_id, const SGTransaction::DAGStruct &dag ) :
        IGeniusTransactions( "escrow", SetDAGWithType( dag, "escrow" ) ), //
        amount_( hold_amount ),                                           //
        job_id_( job_id ),                                                //
        is_release_( false )                                              //
    {
    }
    EscrowTransaction::EscrowTransaction( const std::string &job_id, const SGTransaction::DAGStruct &dag ) :
        IGeniusTransactions( "escrow", SetDAGWithType( dag, "escrow" ) ), //
        amount_( 0 ),                                                     //
        job_id_( job_id ),                                                //
        is_release_( true )                                               //
    {
    }
    std::vector<uint8_t> EscrowTransaction::SerializeByteVector()
    {
        SGTransaction::EscrowTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( this->dag_st );
        tx_struct.set_amount( amount_ );
        tx_struct.set_job_cid( job_id_ );
        tx_struct.set_is_release( is_release_ );
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }
    EscrowTransaction EscrowTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        SGTransaction::EscrowTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse EscrowTx from array." << std::endl;
        }
        uint64_t    amount     = tx_struct.amount();
        std::string job_id     = tx_struct.job_cid();
        bool        is_release = tx_struct.is_release();

        if ( is_release )
        {
            return EscrowTransaction( job_id, tx_struct.dag_struct() ); // Return new instance
        }
        else
        {
            return EscrowTransaction( amount, job_id, tx_struct.dag_struct() ); // Return new instance
        }
    }
    uint64_t EscrowTransaction::GetAmount() const
    {
        return amount_;
    }
}