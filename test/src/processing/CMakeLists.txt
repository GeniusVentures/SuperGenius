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

addtest(task_queue_test
    task_queue_test.cpp
)

target_link_libraries(task_queue_test
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

addtest(processing_payout_metadata_test
    processing_payout_metadata_test.cpp
)

target_include_directories(processing_payout_metadata_test PRIVATE ${GSL_INCLUDE_DIR})

target_link_libraries(processing_payout_metadata_test
    processing_service
    SGProcessingProto
    logger
    Boost::headers
    p2p::p2p_logger
    ipfs-pubsub
)
