addtest(crdt_test
    crdt_hierarchical_key_test.cpp
    crdt_set_test.cpp
    crdt_heads_test.cpp
    crdt_datastore_test.cpp
    crdt_atomic_transaction_test.cpp
    crdt_custom_broadcaster.cpp
    crdt_mirror_broadcaster.cpp
    crdt_custom_dagsyncer.cpp
)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(crdt_test
    crdt_datastore
    crdt_globaldb_proto
    rocksdb
    database_error
    ipfs-lite-cpp::cid
    ipfs-lite-cpp::ipfs_datastore_in_memory
    ipfs-lite-cpp::blake2
    p2p::p2p_cid
    p2p::p2p_multihash
    p2p::p2p_multiaddress
    Boost::headers
    Boost::filesystem
    crdt_heads
)

if(FORCE_MULTIPLE)
  set_target_properties(crdt_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()

addtest(globaldb_integration_test
    globaldb_integration.cpp
)


target_link_libraries(globaldb_integration_test
    crdt_globaldb
)

if(FORCE_MULTIPLE)
  set_target_properties(crdt_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()

# Cold-storage link proof (INTG-01, v1.1 Phase 7): registered ONLY under
# SGNS_COLD_STORAGE because it links sgns-blockstore::sgns-blockstore.
# The Tier1 LABELS assignment below is the fail-closed hermetic-lane
# convention transplanted from
# thirdparty/sgns-blockstore/docs/testing-tiers.md (addtest() itself
# has no LABELS support, so set_tests_properties is mandatory).
if(SGNS_COLD_STORAGE)
addtest(cold_storage_link_proof_test
    cold_storage_link_proof_test.cpp
)

target_link_libraries(cold_storage_link_proof_test
    sgns-blockstore::sgns-blockstore
)

set_tests_properties(cold_storage_link_proof_test PROPERTIES LABELS "Tier1")
endif()

# Cold-storage config-gate matrix (TEST-01 / INTG-02, v1.1 Phase 7 plan 07-03).
# Registered UNCONDITIONALLY - NOT under SGNS_COLD_STORAGE: the parse/env code
# from 07-02 compiles ungated, so the Tier-1 lane is non-empty in default OFF
# builds and ctest -L Tier1 --no-tests=error can never pass vacuously
# (Pitfall 6). Links only crdt_globaldb (rapidjson arrives PRIVATE through it).
addtest(cold_storage_config_gate_test
    cold_storage_config_gate_test.cpp
)

target_link_libraries(cold_storage_config_gate_test
    crdt_globaldb
)

set_tests_properties(cold_storage_config_gate_test PROPERTIES LABELS "Tier1")

# Cold-storage spill journal tests (SPILL-01, v1.1 Phase 8 plan 08-01).
# Registered ONLY under SGNS_COLD_STORAGE: the suite links
# sgns-blockstore::sgns-blockstore (the decorator under test plus the
# InMemoryS3Client fake, whose NOT-installed src/ header is reached via the
# relative include dir below - 08-RESEARCH Pitfall 6). The Tier-1 lane
# stays non-empty on OFF builds via the unconditional config-gate test.
if(SGNS_COLD_STORAGE)
addtest(cold_storage_spill_test
    cold_storage_spill_test.cpp
)

target_link_libraries(cold_storage_spill_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
)

target_include_directories(cold_storage_spill_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../thirdparty/sgns-blockstore/src"
)

set_tests_properties(cold_storage_spill_test PROPERTIES LABELS "Tier1")
endif()

# Cold-read fallback chain tests (READ-01, v1.1 Phase 9 plan 09-01).
# Registered ONLY under SGNS_COLD_STORAGE: the suite links
# sgns-blockstore::sgns-blockstore (the chain service under test plus the
# InMemoryS3Client fake, whose NOT-installed src/ header is reached via the
# relative include dir below - 08-RESEARCH Pitfall 6). The Tier-1 lane
# stays non-empty on OFF builds via the unconditional config-gate test.
if(SGNS_COLD_STORAGE)
addtest(cold_storage_fallback_test
    cold_storage_fallback_test.cpp
)

target_link_libraries(cold_storage_fallback_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
)

target_include_directories(cold_storage_fallback_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../thirdparty/sgns-blockstore/src"
)

set_tests_properties(cold_storage_fallback_test PROPERTIES LABELS "Tier1")
endif()

# Cold-read SC2 latency-budget benchmark + responder-freeze regression
# (READ-02, v1.1 Phase 9 plan 09-02). Registered ONLY under
# SGNS_COLD_STORAGE: links sgns-blockstore::sgns-blockstore plus the
# InMemoryS3Client fake via the relative include dir. The Tier-1 lane
# stays non-empty on OFF builds via the unconditional config-gate test.
if(SGNS_COLD_STORAGE)
addtest(cold_read_freeze_latency_test
    cold_read_freeze_latency_test.cpp
)

target_link_libraries(cold_read_freeze_latency_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
)

target_include_directories(cold_read_freeze_latency_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../thirdparty/sgns-blockstore/src"
)

set_tests_properties(cold_read_freeze_latency_test PROPERTIES LABELS "Tier1")
endif()

# Flush-gated durability advancement tests (DUR-01, v1.1 Phase 10 plan
# 10-01). Registered ONLY under SGNS_COLD_STORAGE: links
# sgns-blockstore::sgns-blockstore plus the InMemoryS3Client and
# InMemoryS3AdminClient fakes via the relative include dir (both fake
# headers are deliberately not installed). The Tier-1 lane stays
# non-empty on OFF builds via the unconditional config-gate test.
if(SGNS_COLD_STORAGE)
addtest(cold_storage_durability_test
    cold_storage_durability_test.cpp
)

target_link_libraries(cold_storage_durability_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
)

target_include_directories(cold_storage_durability_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../thirdparty/sgns-blockstore/src"
)

set_tests_properties(cold_storage_durability_test PROPERTIES LABELS "Tier1")
endif()

# Retention-GC observe-only tests (GC-01 SC1/SC5, v1.1 Phase 11 plan
# 11-01). Registered ONLY under SGNS_COLD_STORAGE: links
# sgns-blockstore::sgns-blockstore plus the InMemoryS3Client and
# InMemoryS3AdminClient fakes via the relative include dir (both fake
# headers are deliberately not installed). The Tier-1 lane stays
# non-empty on OFF builds via the unconditional config-gate test.
if(SGNS_COLD_STORAGE)
addtest(cold_storage_gc_test
    cold_storage_gc_test.cpp
)

target_link_libraries(cold_storage_gc_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
)

target_include_directories(cold_storage_gc_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../thirdparty/sgns-blockstore/src"
)

set_tests_properties(cold_storage_gc_test PROPERTIES LABELS "Tier1")
endif()

