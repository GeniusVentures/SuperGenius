add_proto_library(SGBlockchainProto proto/SGBlockchain.proto)
add_proto_library(ConsensusProto proto/Consensus.proto)
add_proto_library(ConsensusLocalStateProto proto/ConsensusLocalState.proto)
add_proto_library(ValidatorRegistryProto proto/ValidatorRegistry.proto)

target_link_libraries(ConsensusProto
    PUBLIC
    SGTransactionProto
)

add_library(blockchain_genesis
    Blockchain.cpp
    ../ValidatorRegistry.cpp
    ../Consensus.cpp
    ../ConsensusStateStore.cpp
)

target_link_libraries(blockchain_genesis
    PUBLIC
    outcome
    crdt_globaldb
    sgns_version
    buffer
    hasher
    hexutil
    logger
    sgns_genius_account
    ipfs-pubsub
    multisig
    ValidatorRegistryProto
    ConsensusProto
    ConsensusLocalStateProto
    PRIVATE
    SGBlockchainProto
)

supergenius_install(blockchain_genesis)
