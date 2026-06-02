/**
 * @file       MintTransaction.cpp
 * @brief      
 * @date       2024-04-10
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/MintTransaction.hpp"

namespace sgns
{
    MintTransaction::MintTransaction( uint64_t                 new_amount,
                                      std::string              chain_id,
                                      TokenID                  token_id,
                                      SGTransaction::DAGStruct dag ) :
        GeniusTransaction( "mint", SetDAGWithType( std::move( dag ), "mint" ) ),
        amount( new_amount ),
        chain_id( std::move( chain_id ) ),
        token_id( std::move( token_id ) )
    {
    }

    std::vector<uint8_t> MintTransaction::SerializeByteVector( const SGTransaction::DAGStruct &dag ) const
    {
        SGTransaction::MintTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        tx_struct.set_amount( amount );
        tx_struct.set_chain_id( chain_id );
        tx_struct.set_token_id( token_id.bytes().data(), token_id.size() );

        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        if ( !tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            std::cerr << "Failed to serialize transaction\n";
        }

        return serialized_proto;
    }

    EmbeddedTransaction MintTransaction::SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const
    {
        EmbeddedTransaction embedded;
        SGTransaction::MintTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        tx_struct.set_amount( amount );
        tx_struct.set_chain_id( chain_id );
        tx_struct.set_token_id( token_id.bytes().data(), token_id.size() );

        *embedded.mutable_mint() = tx_struct;
        return embedded;
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
        TokenID     tokenid = TokenID::FromBytes( tx_struct.token_id().data(), tx_struct.token_id().size() );

        return std::make_shared<MintTransaction>(
            MintTransaction( amount, chainid, tokenid, tx_struct.dag_struct() ) );
    }

    uint64_t MintTransaction::GetAmount() const
    {
        return amount;
    }

    TokenID MintTransaction::GetTokenID() const
    {
        return token_id;
    }

    std::string MintTransaction::GetChainId() const
    {
        return chain_id;
    }

    MintTransaction MintTransaction::New( uint64_t                 new_amount,
                                          std::string              chain_id,
                                          TokenID                  token_id,
                                          SGTransaction::DAGStruct dag )
    {
        MintTransaction instance( new_amount, std::move( chain_id ), std::move( token_id ), std::move( dag ) );
        instance.FillHash();
        return instance;
    }
}
