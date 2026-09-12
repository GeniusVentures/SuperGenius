/**
 * @file       RegistrationTransaction.cpp
 * @brief      Transaction for child-wallet registration — factory, serialization, deserialization.
 * @date       2026-07-15
 * @author     (Phase 4)
 */
#include "RegistrationTransaction.hpp"

#include <iostream>
#include <utility>

namespace sgns
{
    // ---------------------------------------------------------------------------
    // Constructor
    // ---------------------------------------------------------------------------
    RegistrationTransaction::RegistrationTransaction( std::string                          main_address,
                                                      uint64_t                             sequence,
                                                      SGTransaction::RegistrationMetadata  metadata,
                                                      SGTransaction::DAGStruct             dag,
                                                      bool                                 detach_flag,
                                                      uint64_t                             supersedes_sequence ) :
        GeniusTransaction( "registration", SetDAGWithType( std::move( dag ), "registration" ) ),
        main_address_( std::move( main_address ) ),
        sequence_( sequence ),
        metadata_( std::move( metadata ) ),
        detach_flag_( detach_flag ),
        supersedes_sequence_( supersedes_sequence )
    {
    }

    // ---------------------------------------------------------------------------
    // Factory
    // ---------------------------------------------------------------------------
    RegistrationTransaction RegistrationTransaction::New( std::string                    main_address,
                                                          uint64_t                      sequence,
                                                          SGTransaction::RegistrationMetadata metadata,
                                                          SGTransaction::DAGStruct      dag,
                                                          bool                          detach_flag,
                                                          uint64_t                      supersedes_sequence )
    {
        RegistrationTransaction instance( std::move( main_address ), sequence, std::move( metadata ), std::move( dag ),
                                          detach_flag, supersedes_sequence );
        instance.FillHash();
        return instance;
    }

    // ---------------------------------------------------------------------------
    // Serialize to byte vector
    // ---------------------------------------------------------------------------
    std::vector<uint8_t> RegistrationTransaction::SerializeByteVector( const SGTransaction::DAGStruct &dag ) const
    {
        SGTransaction::RegistrationTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        tx_struct.set_main_address( main_address_ );
        tx_struct.set_sequence( sequence_ );
        tx_struct.mutable_metadata()->CopyFrom( metadata_ );
        tx_struct.set_detach_flag( detach_flag_ );
        tx_struct.set_supersedes_sequence( supersedes_sequence_ );

        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        if ( !tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            std::cerr << "Failed to serialize RegistrationTx\n";
        }

        return serialized_proto;
    }

    // ---------------------------------------------------------------------------
    // Serialize to EmbeddedTransaction (sets the registration = 8 oneof arm)
    // ---------------------------------------------------------------------------
    EmbeddedTransaction RegistrationTransaction::SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const
    {
        EmbeddedTransaction embedded;
        SGTransaction::RegistrationTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        tx_struct.set_main_address( main_address_ );
        tx_struct.set_sequence( sequence_ );
        tx_struct.mutable_metadata()->CopyFrom( metadata_ );
        tx_struct.set_detach_flag( detach_flag_ );
        tx_struct.set_supersedes_sequence( supersedes_sequence_ );

        *embedded.mutable_registration() = tx_struct;
        return embedded;
    }

    // ---------------------------------------------------------------------------
    // Deserialize from byte vector
    // ---------------------------------------------------------------------------
    std::shared_ptr<RegistrationTransaction> RegistrationTransaction::DeSerializeByteVector(
        const std::vector<uint8_t> &data )
    {
        SGTransaction::RegistrationTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse RegistrationTx from array\n";
            return nullptr;
        }

        return std::make_shared<RegistrationTransaction>(
            RegistrationTransaction( tx_struct.main_address(),
                                     tx_struct.sequence(),
                                     tx_struct.metadata(),
                                     tx_struct.dag_struct(),
                                     tx_struct.detach_flag(),
                                     tx_struct.supersedes_sequence() ) );
    }

    // ---------------------------------------------------------------------------
    // Pubsub topics — includes main_address_ for main-node discovery
    // ---------------------------------------------------------------------------
    std::unordered_set<std::string> RegistrationTransaction::GetTopics() const
    {
        auto topics = GeniusTransaction::GetTopics();
        topics.emplace( main_address_ );
        return topics;
    }

}
