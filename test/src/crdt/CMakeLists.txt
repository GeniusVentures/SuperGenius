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

# Retention-GC retrievability proofs (GC-03, v1.1 Phase 11 plan 11-03).
# Registered ONLY under SGNS_COLD_STORAGE: links
# sgns-blockstore::sgns-blockstore plus the InMemoryS3Client and
# InMemoryS3AdminClient fakes via the relative include dir. Proves the
# evicted-CID retrievability legs over the real chain (local read,
# verification honesty, responder re-warm, request cold-before-peers).
if(SGNS_COLD_STORAGE)
addtest(cold_storage_gc_retrievability_test
    cold_storage_gc_retrievability_test.cpp
)

target_link_libraries(cold_storage_gc_retrievability_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
)

target_include_directories(cold_storage_gc_retrievability_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../thirdparty/sgns-blockstore/src"
)

set_tests_properties(cold_storage_gc_retrievability_test PROPERTIES LABELS "Tier1")
endif()

# Fault-race matrix: evict-x-serve-x-drain crossings under deterministic
# barriers + seeded jitter + D-12 audit walk (TEST-02 SC1, v1.1 Phase 12
# plan 12-01). Registered ONLY under SGNS_COLD_STORAGE: links
# sgns-blockstore::sgns-blockstore plus the InMemoryS3Client and
# InMemoryS3AdminClient fakes via the relative include dir (both fake
# headers are deliberately not installed). Hermetic: no live s3d/MinIO
# endpoints, no Docker - the TimingS3Client decorator drives all timing.
if(SGNS_COLD_STORAGE)
addtest(cold_storage_fault_race_test
    cold_storage_fault_race_test.cpp
)

target_link_libraries(cold_storage_fault_race_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
)

target_include_directories(cold_storage_fault_race_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../thirdparty/sgns-blockstore/src"
)

set_tests_properties(cold_storage_fault_race_test PROPERTIES LABELS "Tier1")
endif()


# Resurrection counter-tests (TEST-02 SC2, v1.1 Phase 12 plan 12-01):
# pubsub re-delivery of a GC-evicted pre-delete delta must never
# resurrect the value. Registered ONLY under SGNS_COLD_STORAGE. Links
# the cold-storage set PLUS the pubsub/graphsync surface the full-wire
# two-GlobalDB case needs (the pubsub_graphsync_test recipe:
# genius_node_test whole-archive + p2p_logger + AsyncIOManager include).
if(SGNS_COLD_STORAGE)
addtest(cold_storage_resurrection_test
    cold_storage_resurrection_test.cpp
    crdt_mirror_broadcaster.cpp
)

target_include_directories(cold_storage_resurrection_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../thirdparty/sgns-blockstore/src"
    ${AsyncIOManager_INCLUDE_DIR}
)

target_link_libraries(cold_storage_resurrection_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
    genius_node_test
    p2p::p2p_logger
    logger
)

if(MSVC)
    target_link_options(cold_storage_resurrection_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(cold_storage_resurrection_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(cold_storage_resurrection_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(cold_storage_resurrection_test PROPERTIES LABELS "Tier1")
endif()
# s3d-unavailable soak: outage-then-recovery iteration soak + D-09
# teardown legs + peer cold-fetch budget (TEST-02 SC3, v1.1 Phase 12
# plan 12-02). Registered ONLY under SGNS_COLD_STORAGE: hermetic fakes
# via the relative include dir. The wall-clock soak (Task 3) registers
# separately as Tier3 - NEVER in this lane.
if(SGNS_COLD_STORAGE)
addtest(cold_storage_soak_test
    cold_storage_soak_test.cpp
)

target_link_libraries(cold_storage_soak_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
)

target_include_directories(cold_storage_soak_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../thirdparty/sgns-blockstore/src"
)

set_tests_properties(cold_storage_soak_test PROPERTIES LABELS "Tier1")
endif()
# Dev-gated wall-clock soak (TEST-02 SC3 / D-07, plan 12-02 Task 3):
# env-gated (SGNS_COLD_STORAGE_SOAK_ENABLE), Tier3 - NEVER Tier1, and
# the registration line deliberately carries no Tier1 label. Carries
# the D-09 true kill-9 resume mode (SGNS_COLD_STORAGE_SOAK_RESUME).
if(SGNS_COLD_STORAGE)
addtest(cold_storage_wallclock_soak
    cold_storage_wallclock_soak.cpp
)

target_link_libraries(cold_storage_wallclock_soak
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
)

target_include_directories(cold_storage_wallclock_soak PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../thirdparty/sgns-blockstore/src"
)

set_tests_properties(cold_storage_wallclock_soak PROPERTIES LABELS "Tier3")
endif()