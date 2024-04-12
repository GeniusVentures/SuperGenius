/**
 * @file       TransferTransaction.cpp
 * @brief      
 * @date       2024-04-10
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/TransferTransaction.hpp"

namespace sgns
{
    TransferTransaction::TransferTransaction( const uint256_t &amount, const uint256_t &destination, const SGTransaction::DAGStruct &dag ) :
        IGeniusTransactions( "transfer", SetDAGWithType( dag, "transfer" ) ), //
        encrypted_amount( amount ),                                           //
        dest_address( destination )                                           //
    {
    }
    std::vector<uint8_t> TransferTransaction::SerializeByteVector()
    {
        SGTransaction::TransferTx tx_struct;
        SGTransaction::DAGStruct *dag_struct = tx_struct.mutable_dag_struct();
        dag_struct->set_type( GetType() );
        dag_struct->set_previous_hash( "" );
        dag_struct->set_nonce( 0 );
        dag_struct->set_timestamp( 0 );
        dag_struct->set_uncle_hash( "" );
        dag_struct->set_data_hash( "" );
        tx_struct.set_source_addr( "" );
        tx_struct.set_token_id( 0 );
        tx_struct.set_encrypted_amount( encrypted_amount.str() );
        tx_struct.set_dest_addr( dest_address.str() );
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }
    TransferTransaction TransferTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {

        SGTransaction::TransferTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse TransferTx from array." << std::endl;
        }

        uint256_t            amount(tx_struct.encrypted_amount());
        uint256_t            address(tx_struct.dest_addr());

        return TransferTransaction( amount, address, tx_struct.dag_struct() ); // Return new instance
    }
    template <>
    const std::string TransferTransaction::GetAddress<std::string>() const
    {
        std::ostringstream oss;
        oss << std::hex << dest_address;

        return ( "0x" + oss.str() );
    }
    template <>
    const uint256_t TransferTransaction::GetAddress<uint256_t>() const
    {
        return dest_address;
    }

    template <>
    const std::string TransferTransaction::GetAmount<std::string>() const
    {
        std::ostringstream oss;
        oss << encrypted_amount;

        return ( oss.str() );
    }
    template <>
    const uint256_t TransferTransaction::GetAmount<uint256_t>() const
    {
        return encrypted_amount;
    }
}