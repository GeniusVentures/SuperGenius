
addtest(blockchain_genesis_test
    blockchain_genesis_test.cpp
    )

addtest(consensus_subject_test
    consensus_subject_test.cpp
)
target_link_libraries(consensus_subject_test
    blockchain_genesis
    rapidjson
)

#addtest(consensus_certificate_test
#    consensus_certificate_test.cpp
#)
#target_link_libraries(consensus_certificate_test
#    blockchain_genesis
#    rapidjson
#    base_crdt_test
#)

# ConsensusManager requires 6 constructor dependencies (ValidatorRegistry, GlobalDB,
# GossipPubSub, Signer, etc.) — cannot be instantiated in isolation.
# Test deferred to E2E integration (Phase 4).
# GetSlotKey dispatches via registered handler map — no ConsensusManager instance needed.
addtest(consensus_slot_key_test
    consensus_slot_key_test.cpp
)
target_link_libraries(consensus_slot_key_test
    blockchain_genesis
    genius_node_test
    rapidjson
)

set(CONSENSUS_PENDING_LIFECYCLE_SOURCE consensus_pending_lifecycle)
set(CONSENSUS_PENDING_LIFECYCLE_TARGET consensus_pending_lifecycle)
addtest(consensus_pending_lifecycle_test
    ${CONSENSUS_PENDING_LIFECYCLE_SOURCE}_test.cpp
)
target_link_libraries(${CONSENSUS_PENDING_LIFECYCLE_TARGET}_test
    blockchain_genesis
    genius_node_test
    rapidjson
    base_crdt_test
)

addtest(validator_registry_batch_slot_test
    validator_registry_batch_slot_test.cpp
)
target_link_libraries(validator_registry_batch_slot_test
    blockchain_genesis
    genius_node_test
    rapidjson
    base_crdt_test
)

addtest(multi_node_finality_fault_compatibility_smoke_test
    multi_node_finality_fault_compatibility_smoke_test.cpp
)
target_link_libraries(multi_node_finality_fault_compatibility_smoke_test
    blockchain_genesis
    genius_node_test
    rapidjson
    base_crdt_test
)
set_tests_properties(multi_node_finality_fault_compatibility_smoke_test PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

# Phase 12 owns fixed real-socket ports.  Its launcher is deliberately scoped
# to this target because addtest() expands the executable path and therefore
# cannot consume CMake's target TEST_LAUNCHER property.
if(NOT APPLE OR WIN32)
    message(FATAL_ERROR "Phase 12 process-ownership runner requires supported POSIX platform: fork/setsid/getpgid/getsid/kill/waitpid/socket/bind/sigwait")
endif()
if(CMAKE_VERSION VERSION_LESS 3.29)
    message(FATAL_ERROR "Phase 12 process-ownership runner requires CMake 3.29+ TEST_LAUNCHER support")
endif()
include(CheckSymbolExists)
check_symbol_exists(fork "unistd.h" P12_HAVE_FORK)
check_symbol_exists(setsid "unistd.h" P12_HAVE_SETSID)
check_symbol_exists(getpgid "unistd.h" P12_HAVE_GETPGID)
check_symbol_exists(getsid "unistd.h" P12_HAVE_GETSID)
check_symbol_exists(kill "signal.h" P12_HAVE_KILL)
check_symbol_exists(waitpid "sys/wait.h" P12_HAVE_WAITPID)
check_symbol_exists(socket "sys/socket.h" P12_HAVE_SOCKET)
check_symbol_exists(bind "sys/socket.h;netinet/in.h" P12_HAVE_BIND)
check_symbol_exists(sigwait "signal.h" P12_HAVE_SIGWAIT)
foreach(p12_primitive IN ITEMS FORK SETSID GETPGID GETSID KILL WAITPID SOCKET BIND SIGWAIT)
    if(NOT P12_HAVE_${p12_primitive})
        message(FATAL_ERROR "Phase 12 process-ownership runner unavailable primitive: ${p12_primitive}")
    endif()
endforeach()

add_executable(multi_node_finality_fault_runner
    multi_node_finality_fault_runner.cpp
)
set_target_properties(multi_node_finality_fault_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_bin
)
disable_clang_tidy(multi_node_finality_fault_runner)

add_executable(multi_node_finality_fault_test)
addtest_part(multi_node_finality_fault_test
    multi_node_finality_fault_test.cpp
)
target_link_libraries(multi_node_finality_fault_test
    blockchain_genesis
    genius_node_test
    rapidjson
    base_crdt_test
    GTest::gtest_main
    GTest::gmock_main
)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/xunit)
set(multi_node_finality_fault_xml_output
    "--gtest_output=xml:${CMAKE_BINARY_DIR}/xunit/xunit-multi_node_finality_fault_test.xml"
)
add_test(
    NAME multi_node_finality_fault_test
    COMMAND multi_node_finality_fault_test ${multi_node_finality_fault_xml_output}
)
set_target_properties(multi_node_finality_fault_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_bin
    ARCHIVE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/test_lib
    LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/test_lib
    TEST_LAUNCHER "$<TARGET_FILE:multi_node_finality_fault_runner>"
)
disable_clang_tidy(multi_node_finality_fault_test)
add_dependencies(multi_node_finality_fault_test multi_node_finality_fault_runner)
set_tests_properties(multi_node_finality_fault_test PROPERTIES
    TIMEOUT 300
    RUN_SERIAL TRUE
    RESOURCE_LOCK phase12_real_socket_ports
)

add_test(
    NAME multi_node_finality_fault_process_ownership_test
    COMMAND $<TARGET_FILE:multi_node_finality_fault_runner>
            --verify-controlled-cancellation
            $<TARGET_FILE:multi_node_finality_fault_test>
)
set_tests_properties(multi_node_finality_fault_process_ownership_test PROPERTIES
    TIMEOUT 60
    RUN_SERIAL TRUE
    RESOURCE_LOCK phase12_real_socket_ports
)

target_include_directories(blockchain_genesis_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(blockchain_genesis_test
    genius_node_test
    rapidjson
)
if(WIN32)
    target_link_options(blockchain_genesis_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(blockchain_genesis_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(blockchain_genesis_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()
