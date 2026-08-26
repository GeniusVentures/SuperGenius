addtest(token_id_test
    token_id_test.cpp
)

addtest(token_amount_test
    token_amount_test.cpp
)

target_link_libraries(token_amount_test
    genius_node_test
)

addtest(utxo_manager_test
    utxo_manager_test.cpp
)

target_link_libraries(utxo_manager_test
    genius_node_test
    json_secure_storage
    base_rocksdb_test
)

if(MSVC)
    target_link_options(utxo_manager_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(utxo_manager_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(utxo_manager_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(account_management_test
    account_management_test.cpp
)

target_link_libraries(account_management_test genius_node_test json_secure_storage)

# Absolute path to the shared processing test assets. Passed in rather than derived
# from the binary location at runtime: that walked a fixed number of parent
# directories and so silently depended on the build layout (the Visual Studio
# per-config subdirectory, or the ABI subdirectory on Linux) supplying a level.
target_compile_definitions(account_management_test PRIVATE
    SGNS_PROCESSING_ASSETS_DIR="${PROJECT_ROOT}/test/src/processing_nodes")

addtest(result_gc_test
    result_gc_test.cpp
)

target_link_libraries(result_gc_test
    GTest::gtest_main
)

if(MSVC)
    target_link_options(account_management_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(account_management_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(account_management_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(account_signature_test
    account_signature_test.cpp
)

target_link_libraries(account_signature_test
    sgns_genius_account
    json_secure_storage
    # Kept only so this test can cross-check the crypto3-free implementation
    # against the legacy crypto3/ProofSystem derivation.
    ProofSystem
)

addtest(burnconfig_test
    burnconfig_test.cpp
)

target_link_libraries(burnconfig_test
    burnconfig
    trustedpeer
    securecrdt
    multisig
    crdt_globaldb
    sgns_genius_account
    json_secure_storage
    libsecp256k1::secp256k1
)
target_include_directories(burnconfig_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

addtest(bridge_relayer_test
    bridge_relayer_test.cpp
)

target_link_libraries(bridge_relayer_test
    genius_node_test
)

if(MSVC)
    target_link_options(bridge_relayer_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(bridge_relayer_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(bridge_relayer_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(chain_rpc_endpoint_provider_test
    chain_rpc_endpoint_provider_test.cpp
)

target_link_libraries(chain_rpc_endpoint_provider_test
    genius_node_test
)

# Phase 6 (D-01): slot-hash accessor on PublicChainInputValidator.
addtest(public_chain_input_validator_slot_test
    public_chain_input_validator_slot_test.cpp
)

target_link_libraries(public_chain_input_validator_slot_test
    genius_node_test
)

if(MSVC)
    target_link_options(public_chain_input_validator_slot_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(public_chain_input_validator_slot_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(public_chain_input_validator_slot_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

if(MSVC)
    target_link_options(chain_rpc_endpoint_provider_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(chain_rpc_endpoint_provider_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(chain_rpc_endpoint_provider_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set(TRANSACTION_MANAGER_PENDING_LIFECYCLE_SOURCE transaction_manager_pending_lifecycle)
set(TRANSACTION_MANAGER_PENDING_LIFECYCLE_TARGET transaction_manager_pending_lifecycle)
addtest(transaction_manager_pending_lifecycle_test
    ${TRANSACTION_MANAGER_PENDING_LIFECYCLE_SOURCE}_test.cpp
)
target_link_libraries(${TRANSACTION_MANAGER_PENDING_LIFECYCLE_TARGET}_test
    genius_node_test
    json_secure_storage
    base_crdt_test
)

if(MSVC)
    target_link_options(${TRANSACTION_MANAGER_PENDING_LIFECYCLE_TARGET}_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(${TRANSACTION_MANAGER_PENDING_LIFECYCLE_TARGET}_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(${TRANSACTION_MANAGER_PENDING_LIFECYCLE_TARGET}_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(transaction_manager_certificate_fallback_test
    transaction_manager_certificate_fallback_test.cpp
)

target_link_libraries(transaction_manager_certificate_fallback_test
    genius_node
    base_crdt_test
)

if(MSVC)
    target_link_options(transaction_manager_certificate_fallback_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)
elseif(APPLE)
    target_link_options(transaction_manager_certificate_fallback_test PUBLIC -force_load "$<TARGET_FILE:genius_node>")
else()
    target_link_options(transaction_manager_certificate_fallback_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(network_config_precedence_test
    network_config_precedence_test.cpp
)

target_link_libraries(network_config_precedence_test genius_node json_secure_storage)

if(MSVC)
    target_link_options(network_config_precedence_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)
elseif(APPLE)
    target_link_options(network_config_precedence_test PUBLIC -force_load "$<TARGET_FILE:genius_node>")
else()
    target_link_options(network_config_precedence_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(node_type_derivation_test
    node_type_derivation_test.cpp
)

target_link_libraries(node_type_derivation_test genius_node json_secure_storage)

if(MSVC)
    target_link_options(node_type_derivation_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)
elseif(APPLE)
    target_link_options(node_type_derivation_test PUBLIC -force_load "$<TARGET_FILE:genius_node>")
else()
    target_link_options(node_type_derivation_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()
