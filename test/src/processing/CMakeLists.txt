# addtest(processing_service_test
    # processing_service_test.cpp
    # processing_engine_test.cpp
    # processing_subtask_queue_accessor_impl_test.cpp
    # processing_subtask_queue_channel_pubsub_test.cpp
    # processing_subtask_queue_manager_test.cpp
    # processing_subtask_validation_test.cpp
# )

# target_include_directories(processing_service_test PRIVATE ${GSL_INCLUDE_DIR})

# target_link_libraries(processing_service_test
#     SGProcessingProto
#     processing_service
#     logger
#     Boost::headers
#     p2p::p2p_logger
#     ipfs-pubsub
# )

# 15-13: standalone registration of the two previously-commented test targets.
# The source lists DIFFER per target and that split is load-bearing:
# - processing_service_test self-contains the ProcessingServiceTest fixture method
#   definitions (SetUp/SetUp(name,config)/TearDown/Initialize), so it MUST NOT also
#   compile processing_service_test_base.cpp (duplicate symbols);
# - processing_subtask_queue_channel_pubsub_test's fixture INHERITS ProcessingServiceTest
#   and calls its SetUp/Initialize, which resolve ONLY from base.cpp for this target.
# The remaining four commented sources stay unregistered (out of 15-13 scope).
addtest(processing_service_test
    processing_service_test.cpp
)

target_include_directories(processing_service_test PRIVATE ${GSL_INCLUDE_DIR})

target_link_libraries(processing_service_test
    SGProcessingProto
    processing_service
    logger
    Boost::headers
    p2p::p2p_logger
    ipfs-pubsub
)

addtest(processing_subtask_queue_channel_pubsub_test
    processing_subtask_queue_channel_pubsub_test.cpp
    processing_service_test_base.cpp
)

target_include_directories(processing_subtask_queue_channel_pubsub_test PRIVATE ${GSL_INCLUDE_DIR})

target_link_libraries(processing_subtask_queue_channel_pubsub_test
    processing_service
    SGProcessingProto
    logger
    Boost::headers
    p2p::p2p_logger
    ipfs-pubsub
)

addtest(task_queue_test
    task_queue_test.cpp
)

target_link_libraries(task_queue_test
    processing_service
    SGProcessingProto
    base_crdt_test
)

addtest(task_keys_scope_test
    task_keys_scope_test.cpp
)

target_link_libraries(task_keys_scope_test
    processing_service
    SGProcessingProto
    base_crdt_test
)

addtest(processing_result_durability_test
    processing_result_durability_test.cpp
    processing_service_test_base.cpp
)

target_include_directories(processing_result_durability_test PRIVATE ${GSL_INCLUDE_DIR})

target_link_libraries(processing_result_durability_test
    processing_service
    SGProcessingProto
    ipfs-bitswap-cpp
    logger
    Boost::headers
    p2p::p2p_logger
    ipfs-pubsub
)

addtest(processing_validate_result_data_test
    processing_validate_result_data_test.cpp
    processing_service_test_base.cpp
)

target_include_directories(processing_validate_result_data_test PRIVATE ${GSL_INCLUDE_DIR})

target_link_libraries(processing_validate_result_data_test
    processing_service
    SGProcessingProto
    logger
    Boost::headers
    p2p::p2p_logger
    ipfs-pubsub
)

addtest(processing_core_gating_test
    processing_core_gating_test.cpp
)

target_link_libraries(processing_core_gating_test
    processing_service
    SGProcessingProto
    logger
    Boost::headers
    p2p::p2p_logger
    ipfs-pubsub
)
