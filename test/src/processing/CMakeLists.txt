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

# Phase 01-03 (elmbridge, FUND-02): derived ELM lock-timeout wiring tests.
# Queue-level cases use ProcessingSubTaskQueue directly; queue-manager cases
# use the processing_mock.hpp channel (same pattern as
# processing_subtask_queue_manager_test). One case per process at verification
# time (isolated --gtest_filter runs) per libp2p teardown guidance.
addtest(elm_lock_timeout_test
    elm_lock_timeout_test.cpp
)

target_link_libraries(elm_lock_timeout_test
    processing_service
    SGProcessingProto
    ipfs-pubsub
)

# Phase 01-03 (elmbridge, JOB-03/D-12): validation-mode defensive-assertion
# matrix. Pure parse-level cases (no queue, no harness).
addtest(elm_validation_mode_test
    elm_validation_mode_test.cpp
)

target_link_libraries(elm_validation_mode_test
    processing_service
    SGProcessingProto
    ipfs-pubsub
)

# elmbridge 04-03 (JOB-02): splitter mapping/map-tolerance/submit-acceptance.
# The submit leg runs a full node (ElmCostNode pattern): genius_node_test
# harness + WHOLEARCHIVE on MSVC; TIMEOUT 600 for node startup.
addtest(elm_splitter_test
    elm_splitter_test.cpp
)

target_link_libraries(elm_splitter_test
    processing_service
    SGProcessingProto
    genius_node_test
    json_secure_storage
)

if(MSVC)
    target_link_options(elm_splitter_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(elm_splitter_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(elm_splitter_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

# elmbridge 04-04 (FUND-03/OD-3): settlement arithmetic matrix + envelope
# filter + BuildPayoutOutputs ELM branch/parity. PURE unit legs (no node);
# BuildPayoutOutputs reached via the test-access friend class (the
# payout_outputs_test pattern — lighter than WHOLEARCHIVE: TransactionManager
# symbols come from genius_node_test's link closure).
addtest(elm_settlement_test
    elm_settlement_test.cpp
)

target_link_libraries(elm_settlement_test
    processing_service
    SGProcessingProto
    genius_node_test
    json_secure_storage
)

if(MSVC)
    target_link_options(elm_settlement_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(elm_settlement_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(elm_settlement_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(elm_settlement_test PROPERTIES TIMEOUT 120)

# elmbridge 04-05 (E2E-01..E2E-03, milestone acceptance): empty-cache
# single-node E2E + overtime leg + non-ELM regression gate.
# SGPROC_ELM_TEST_MODEL_DIR-gated (GTEST_SKIP with the README cross-ref);
# TIMEOUT 1800 = ~4x margin over the ~557MB-download leg (P4-10).
addtest(elm_e2e_test
    elm_e2e_test.cpp
)

target_link_libraries(elm_e2e_test
    processing_service
    SGProcessingProto
    genius_node_test
    json_secure_storage
)

if(MSVC)
    target_link_options(elm_e2e_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(elm_e2e_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(elm_e2e_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(elm_e2e_test PROPERTIES TIMEOUT 1800)

# Phase 19 (VALD-02): points the new Vald02* TEST cases at this phase's fresh, real, already
# checked-in cross-machine .cap fixture pair, living in the outer repo's .planning tree -- not
# copied or duplicated into SuperGenius (D-01-REVISED). Four "../" levels walk from this directory
# (SuperGenius/test/src/processing) up through SuperGenius/test, SuperGenius/, to the outer repo
# root, landing in .planning/workstreams/sgproc-render/.... Originally pointed at Phase 13's
# archived captures dir; updated to this phase's own captures/ dir after Phase 13's original .cap
# files were found to be unreadable by current tooling (a pre-Phase-16 manifest-format backward
# compatibility regression, unrelated to this phase -- see 19-CONTEXT.md D-01-REVISED) and a fresh
# 2-machine capture was taken instead.
# Phase 19 (VALD-02): the Vald02* TEST cases read a real cross-machine .cap fixture pair.
# The fixtures now live IN this repo under fixtures/ (tracked via Git LFS, see .gitattributes)
# instead of reaching out of the SuperGenius submodule into the outer repo's .planning tree —
# the old "../../../../.planning/..." path only worked in a checkout that had that planning
# workspace present and LFS-pulled, breaking standalone SuperGenius builds and CI.
target_compile_definitions(processing_validation_core_test PRIVATE
    VALD02_CAPTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
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
