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

addtest(processing_validation_core_test
    processing_validation_core_test.cpp
)

target_link_libraries(processing_validation_core_test
    processing_service
    SGProcessingProto
    sgproccapture
)

# Phase 19 (VALD-02): points the new Vald02* TEST cases at Phase 13's real, already-checked-in
# cross-machine .cap fixture pair, living in the outer repo's .planning tree -- not copied or
# duplicated into SuperGenius (D-01). Four "../" levels walk from this directory
# (SuperGenius/test/src/processing) up through SuperGenius/test, SuperGenius/, to the outer repo
# root, landing in .planning/workstreams/sgproc-render/....
target_compile_definitions(processing_validation_core_test PRIVATE
    VALD02_CAPTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../../.planning/workstreams/sgproc-render/phases/13-re-validation-scope-boundary-documentation/captures"
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
