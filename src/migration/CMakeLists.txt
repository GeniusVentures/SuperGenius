add_library(migration STATIC
    MigrationManager.cpp
    Migration0_2_0To1_0_0.cpp
    Migration1_0_0To3_4_0.cpp
    Migration3_4_0To3_5_0.cpp
    Migration3_5_0To3_6_0.cpp
    Migration3_6_0To3_7_0.cpp
)
target_link_libraries(migration
    PUBLIC
    sgns_genius_account
    PRIVATE
    SGTransactionProto
    SGProcessingProto
    # Migration3_6_0To3_7_0.cpp pulls blockchain/Blockchain.hpp ->
    # Consensus.hpp -> ValidatorRegistry.hpp, whose generated headers live in
    # the shared generated/ dir. Without these link edges the TU only FOUND the
    # headers via another proto target's include dir, with no ordering
    # dependency on their generation -- a build-graph race that CI job order
    # can flip (seen on Linux-aarch64 Debug).
    ConsensusProto
    ValidatorRegistryProto
    SGBlockchainProto
)
supergenius_install(migration)
