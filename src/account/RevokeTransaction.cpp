/**
 * @file       RevokeTransaction.cpp
 * @brief      Transaction for main-initiated revocation of a registered child wallet — factory, serialization, deserialization.
 * @date       2026-07-21
 * @author     (Phase 5)
 */
#include "RevokeTransaction.hpp"

#include <iostream>
#include <utility>

namespace sgns
{
    // ---------------------------------------------------------------------------
    // Constructor
    // ---------------------------------------------------------------------------
    RevokeTransaction::RevokeTransaction( std::string               child_address,
                                          uint64_t                  registration_sequence,
                                          SGTransaction::DAGStruct  dag ) :
        GeniusTransaction( "revoke", SetDAGWithType( std::move( dag ), "revoke" ) ),
        child_address_( std::move( child_address ) ),
        registration_sequence_( registration_sequence )
    {
    }

    // ---------------------------------------------------------------------------
    // Factory
    // ---------------------------------------------------------------------------
    RevokeTransaction RevokeTransaction::New( std::string               child_address,
                                              uint64_t                  registration_sequence,
                                              SGTransaction::DAGStruct  dag )
    {
        RevokeTransaction instance( std::move( child_address ), registration_sequence, std::move( dag ) );
        instance.FillHash();
        return instance;
    }

    // ---------------------------------------------------------------------------
    // Serialize to byte vector
    // ---------------------------------------------------------------------------
    std::vector<uint8_t> RevokeTransaction::SerializeByteVector( const SGTransaction::DAGStruct &dag ) const
    {
        SGTransaction::RevokeTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        tx_struct.set_child_address( child_address_ );
        tx_struct.set_registration_sequence( registration_sequence_ );

        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        if ( !tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            std::cerr << "Failed to serialize RevokeTx\n";
        }

        return serialized_proto;
    }

    // ---------------------------------------------------------------------------
    // Serialize to EmbeddedTransaction (sets the revoke = 9 oneof arm)
    // ---------------------------------------------------------------------------
    EmbeddedTransaction RevokeTransaction::SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const
    {
        EmbeddedTransaction embedded;
        SGTransaction::RevokeTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        tx_struct.set_child_address( child_address_ );
        tx_struct.set_registration_sequence( registration_sequence_ );

        *embedded.mutable_revoke() = tx_struct;
        return embedded;
    }

    // ---------------------------------------------------------------------------
    // Deserialize from byte vector
    // ---------------------------------------------------------------------------
    std::shared_ptr<RevokeTransaction> RevokeTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        SGTransaction::RevokeTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse RevokeTx from array\n";
            return nullptr;
        }

        return std::make_shared<RevokeTransaction>(
            RevokeTransaction( tx_struct.child_address(),
                               tx_struct.registration_sequence(),
                               tx_struct.dag_struct() ) );
    }

    // ---------------------------------------------------------------------------
    // Pubsub topics — includes child_address_ for target-child discovery
    // ---------------------------------------------------------------------------
    std::unordered_set<std::string> RevokeTransaction::GetTopics() const
    {
        auto topics = GeniusTransaction::GetTopics();
        topics.emplace( child_address_ );
        return topics;
    }

}
