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
)
supergenius_install(migration)
