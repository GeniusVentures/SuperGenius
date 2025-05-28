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
    MintTransaction::MintTransaction( uint64_t                 new_amount,
                                      std::string              chain_id,
                                      std::string              token_id,
                                      SGTransaction::DAGStruct dag ) :
        IGeniusTransactions( "mint", SetDAGWithType( std::move( dag ), "mint" ) ),
        amount( new_amount ),
        chain_id( std::move( chain_id ) ),
        token_id( std::move( token_id ) )
    {
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
            std::cerr << "Failed to parse TransferTx from array\n";
            return nullptr;
        }
        uint64_t    amount  = tx_struct.amount();
        std::string chainid = tx_struct.chain_id();
        std::string tokenid = tx_struct.token_id();
        //std::memcpy( &v64, &( *data.begin() ), sizeof( v64 ) );

        return std::make_shared<MintTransaction>(
            MintTransaction( amount, chainid, tokenid, tx_struct.dag_struct() ) ); // Return new instance
    }

    uint64_t MintTransaction::GetAmount() const
    {
        return amount;
    }

    std::string MintTransaction::GetTokenID() const
    {
        return token_id;
    }

    MintTransaction MintTransaction::New( uint64_t                                        new_amount,
                                          std::string                                     chain_id,
                                          std::string                                     token_id,
                                          SGTransaction::DAGStruct                        dag,
                                          std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key )
    {
        MintTransaction instance( new_amount, std::move( chain_id ), std::move( token_id ), std::move( dag ) );
        instance.FillHash();
        instance.MakeSignature( std::move( eth_key ) );
        return instance;
    }
}
