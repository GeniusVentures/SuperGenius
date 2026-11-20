addtest(crdt_test
    crdt_hierarchical_key_test.cpp
    crdt_set_test.cpp
    crdt_heads_test.cpp
    crdt_datastore_test.cpp
    crdt_atomic_transaction_test.cpp
    crdt_erase_test.cpp
    crdt_priority_delta_keys_test.cpp
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
    # crdt_priority_delta_keys_test.cpp exercises the GlobalDB facade on a
    # real store; the GlobalDB symbols live only in crdt_globaldb.
    crdt_globaldb
    rocksdb
    database_error
    ipfs-lite-cpp::cid
    ipfs-lite-cpp::ipfs_datastore_in_memory
    ipfs-lite-cpp::ipfs_datastore_rocksdb
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

# Source root scanned at runtime by EraseSurfaceGateTest (crdt_erase_test.cpp):
# the repo's production src/ tree. PROJECT_ROOT is the repo root set by
# build/CommonCompilerOptions.cmake; CMAKE_SOURCE_DIR is NOT used because this
# repo drives CMake from build/<platform>/ (an out-of-root driver directory).
target_compile_definitions(crdt_test PRIVATE
    SUPERGNUS_ERASE_GATE_SRC="${PROJECT_ROOT}/src"
)

addtest(globaldb_integration_test
    globaldb_integration.cpp
    globaldb_erase_test.cpp
)


target_link_libraries(globaldb_integration_test
    crdt_globaldb
)

# Cold-enabled erase-gate legs (v1.2 Phase 15 plan 15-03, SERVE-04/D-07):
# the cold-enabled refusal-failover leg links the InMemory S3 fakes +
# the cold-stack client-override seam, which live in sgns-blockstore
# (the node_startup_test gate discipline). Registered ONLY under
# SGNS_COLD_STORAGE; the OFF build keeps the suite at its pre-15-03
# cold-disabled shape.
if(SGNS_COLD_STORAGE)
    target_compile_definitions(globaldb_integration_test PRIVATE SGNS_COLD_STORAGE)
    # sgns-blockstore: the InMemory S3 fakes + the cold-stack override
    # seam. rapidjson + SGTransactionProto: the failover leg includes
    # PruneEligibility.hpp (rapidjson/document.h) and hand-rolls the
    # PrunedBlockRecord evidence row (the helper lives in
    # genius_transaction_objs, which this crdt-level target must not
    # link - the layering ruling in src/crdt/globaldb/CMakeLists.txt) -
    # both arrive PRIVATE through crdt_globaldb, so linked directly.
    target_link_libraries(globaldb_integration_test
        sgns-blockstore::sgns-blockstore
        rapidjson
        SGTransactionProto
    )
endif()

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
# InMemoryS3Client fake - installed WITH the library since the 12-03
# header-install ruling). The Tier-1 lane
# stays non-empty on OFF builds via the unconditional config-gate test.
if(SGNS_COLD_STORAGE)
addtest(cold_storage_spill_test
    cold_storage_spill_test.cpp
)

target_link_libraries(cold_storage_spill_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
)


set_tests_properties(cold_storage_spill_test PROPERTIES LABELS "Tier1")
endif()

# Cold-read fallback chain tests (READ-01, v1.1 Phase 9 plan 09-01).
# Registered ONLY under SGNS_COLD_STORAGE: the suite links
# sgns-blockstore::sgns-blockstore (the chain service under test plus the
# InMemoryS3Client fake - installed WITH the library since the 12-03
# header-install ruling). The Tier-1 lane
# stays non-empty on OFF builds via the unconditional config-gate test.
if(SGNS_COLD_STORAGE)
addtest(cold_storage_fallback_test
    cold_storage_fallback_test.cpp
)

target_link_libraries(cold_storage_fallback_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
)


set_tests_properties(cold_storage_fallback_test PROPERTIES LABELS "Tier1")
endif()

# Cold-read SC2 latency-budget benchmark + responder-freeze regression
# (READ-02, v1.1 Phase 9 plan 09-02). Registered ONLY under
# SGNS_COLD_STORAGE: links sgns-blockstore::sgns-blockstore plus the
# InMemoryS3Client fake (library-installed header). The Tier-1 lane
# stays non-empty on OFF builds via the unconditional config-gate test.
if(SGNS_COLD_STORAGE)
addtest(cold_read_freeze_latency_test
    cold_read_freeze_latency_test.cpp
)

target_link_libraries(cold_read_freeze_latency_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
)


set_tests_properties(cold_read_freeze_latency_test PROPERTIES LABELS "Tier1")
endif()

# Flush-gated durability advancement tests (DUR-01, v1.1 Phase 10 plan
# 10-01). Registered ONLY under SGNS_COLD_STORAGE: links
# sgns-blockstore::sgns-blockstore plus the InMemoryS3Client and
# InMemoryS3AdminClient fakes (library-installed headers). The Tier-1
# lane stays
# non-empty on OFF builds via the unconditional config-gate test.
if(SGNS_COLD_STORAGE)
addtest(cold_storage_durability_test
    cold_storage_durability_test.cpp
)

target_link_libraries(cold_storage_durability_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
)


set_tests_properties(cold_storage_durability_test PROPERTIES LABELS "Tier1")
endif()

# Retention-GC observe-only tests (GC-01 SC1/SC5, v1.1 Phase 11 plan
# 11-01). Registered ONLY under SGNS_COLD_STORAGE: links
# sgns-blockstore::sgns-blockstore plus the InMemoryS3Client and
# InMemoryS3AdminClient fakes (library-installed headers). The Tier-1
# lane stays
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


set_tests_properties(cold_storage_gc_test PROPERTIES LABELS "Tier1")
endif()

# Retention-GC retrievability proofs (GC-03, v1.1 Phase 11 plan 11-03).
# Registered ONLY under SGNS_COLD_STORAGE: links
# sgns-blockstore::sgns-blockstore plus the InMemoryS3Client and
# InMemoryS3AdminClient fakes (library-installed headers). Proves the
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


set_tests_properties(cold_storage_gc_retrievability_test PROPERTIES LABELS "Tier1")
endif()

# Fault-race matrix: evict-x-serve-x-drain crossings under deterministic
# barriers + seeded jitter + D-12 audit walk (TEST-02 SC1, v1.1 Phase 12
# plan 12-01). Registered ONLY under SGNS_COLD_STORAGE: links
# sgns-blockstore::sgns-blockstore plus the InMemoryS3Client and
# InMemoryS3AdminClient fakes (library-installed headers). Hermetic:
# no live s3d/MinIO
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

# Cold-durability prune admission gate (PRUNE-01/PRUNE-03, v1.2 Phase 14
# plan 14-01): TxPruner::RunCycle's group filter admits an erase only on
# evidence-Durable cold state; everything else defers the whole group as a
# counter. Registered ONLY under SGNS_COLD_STORAGE. Links the cold-storage
# set PLUS the transaction/account surface the TxPruner + TransactionManager
# fixture needs (the tx_pruner_test recipe: genius_node_test whole-archive
# + p2p_logger, over the resurrection-test cold link set).
if(SGNS_COLD_STORAGE)
addtest(cold_prune_gate_test
    cold_prune_gate_test.cpp
)

target_include_directories(cold_prune_gate_test PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

target_link_libraries(cold_prune_gate_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
    genius_node_test
    p2p::p2p_logger
    logger
)

if(MSVC)
    target_link_options(cold_prune_gate_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(cold_prune_gate_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(cold_prune_gate_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(cold_prune_gate_test PROPERTIES LABELS "Tier1")
endif()

# Red-first TOCTOU counter-test (PRUNE-02, v1.2 Phase 14 plan 14-02): a
# durability regression induced between gate admission and the erase call
# is refused by the in-compose cold-durability re-check; the refusal is a
# counter, never an error. Registered ONLY under SGNS_COLD_STORAGE; same
# link set as cold_prune_gate_test (genius_node_test whole-archive over
# the resurrection-test cold set).
if(SGNS_COLD_STORAGE)
addtest(cold_prune_toctou_test
    cold_prune_toctou_test.cpp
)

target_include_directories(cold_prune_toctou_test PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

target_link_libraries(cold_prune_toctou_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
    genius_node_test
    p2p::p2p_logger
    logger
)

if(MSVC)
    target_link_options(cold_prune_toctou_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(cold_prune_toctou_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(cold_prune_toctou_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(cold_prune_toctou_test PROPERTIES LABELS "Tier1")
endif()

# Evidence-aware cold-serving matrix (SERVE-01/02/04 + TEST-01, v1.2
# Phase 15 plan 15-01): the /prune/blk evidence consult refuses cold
# serves for erased CIDs (distinct terminal errors, no resurrection),
# serves no-evidence cold bytes verbatim (evict = DETACH), fails closed
# on malformed rows, stages BOTH marker rows atomically through a real
# TxPruner erase, and proves the cold-disabled build never consults
# evidence. Registered ONLY under SGNS_COLD_STORAGE; same link set as
# cold_prune_gate_test (the TxPruner/TransactionManager fixture needs
# the genius_node_test whole-archive) plus the in-memory datastore the
# re-warm gate leg uses.
if(SGNS_COLD_STORAGE)
addtest(cold_evidence_matrix_test
    cold_evidence_matrix_test.cpp
)

target_include_directories(cold_evidence_matrix_test PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

target_link_libraries(cold_evidence_matrix_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
    genius_node_test
    p2p::p2p_logger
    logger
    ipfs-lite-cpp::ipfs_datastore_in_memory
)

if(MSVC)
    target_link_options(cold_evidence_matrix_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(cold_evidence_matrix_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(cold_evidence_matrix_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(cold_evidence_matrix_test PROPERTIES LABELS "Tier1")
endif()

# TxPruner x GC composition hardening (COMP-01/02/03, v1.2 Phase 16): the
# SC1 stale-LRU window close proven at both D-06 layers (deterministic
# observe-and-continue seam + barrier-driven real-thread), the abort-path
# and absent-key edges, the SC2 interleave matrix, and the SC3
# disposal-starvation deterministic leg. Registered ONLY under
# SGNS_COLD_STORAGE; same link set as cold_prune_gate_test (the
# genius_node_test whole-archive keeps PruneEligibility test helpers
# linkable for the later 16-02/16-03 legs).
if(SGNS_COLD_STORAGE)
addtest(cold_compose_matrix_test
    cold_compose_matrix_test.cpp
)

target_include_directories(cold_compose_matrix_test PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

target_link_libraries(cold_compose_matrix_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
    genius_node_test
    p2p::p2p_logger
    logger
)

if(MSVC)
    target_link_options(cold_compose_matrix_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(cold_compose_matrix_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(cold_compose_matrix_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(cold_compose_matrix_test PROPERTIES LABELS "Tier1")
endif()

# GC walk-gap evidence exemption counter-test (D-01/D-03, v1.2 Phase 18
# plan 18-01): RED-FIRST Tier-1 suite proving the poisoned-GC-triangle is
# defused — LEG-1 (evidence + hot gone + cold present) and LEG-2 (cold
# deleted both tiers via the fake's test-side deletion) must complete the
# walk through the exemption while the NoEvidence / Malformed controls
# keep today's fail-closed abort. Registered ONLY under SGNS_COLD_STORAGE;
# same link set as cold_compose_matrix_test (the genius_node_test
# whole-archive keeps the PruneEligibility sanctioned key builder
# linkable for the evidence-seeding helper).
if(SGNS_COLD_STORAGE)
addtest(cold_gc_walk_gap_exempt_test
    cold_gc_walk_gap_exempt_test.cpp
)

target_include_directories(cold_gc_walk_gap_exempt_test PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

target_link_libraries(cold_gc_walk_gap_exempt_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
    genius_node_test
    p2p::p2p_logger
    logger
)

if(MSVC)
    target_link_options(cold_gc_walk_gap_exempt_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(cold_gc_walk_gap_exempt_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(cold_gc_walk_gap_exempt_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(cold_gc_walk_gap_exempt_test PROPERTIES LABELS "Tier1")
endif()

# Downgrade interlock verdict matrix (D-05/D-06/D-07, v1.2 Phase 18 plan
# 18-02): the persisted role fingerprint + ack key + disarm gate pinned leg
# by leg on a REAL GlobalDB raw surface — FP-FirstRun/FP-DowngradeFire/
# FP-AckArms/FP-FullFires/FP-UnreadableDisarm/FP-SamePosture + the
# CFG-AckParse unit. Registered ONLY under SGNS_COLD_STORAGE; same link
# set as cold_evidence_matrix_test (the genius_node_test whole-archive
# keeps the PruneEligibility helpers + TransactionManager fixture
# linkable). The 18-03 t0->t3 timeline legs extend this same file.
if(SGNS_COLD_STORAGE)
addtest(cold_downgrade_e2e_test
    cold_downgrade_e2e_test.cpp
)

target_include_directories(cold_downgrade_e2e_test PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

target_compile_definitions(cold_downgrade_e2e_test PRIVATE
    SUPERGNUS_DOWNGRADE_GATE_SRC="${PROJECT_ROOT}/src"
)

target_link_libraries(cold_downgrade_e2e_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
    genius_node_test
    p2p::p2p_logger
    logger
    ipfs-lite-cpp::ipfs_datastore_in_memory
)

if(MSVC)
    target_link_options(cold_downgrade_e2e_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(cold_downgrade_e2e_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(cold_downgrade_e2e_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(cold_downgrade_e2e_test PROPERTIES LABELS "Tier1")
endif()

# Dev-gated live downgrade smoke (D-03/D-11, v1.2 Phase 18 plan 18-03):
# the compact downgrade timeline on a REAL MinIO community server through
# the PRODUCTION S3 client — archive seed, light re-open interlock,
# ack-armed prune cycle, one post-prune GC pass surviving via the 18-01
# exemption. Env-gated opt-in via SGNS_BLOCKSTORE_TEST_MINIO_BIN
# (community build; loud labelled skip without it) — NEVER in the Tier-1
# CI lane: registered LABELS "Tier3" (the cold_storage_wallclock_soak
# env-gated convention; Pitfall 6 label parity holds).
if(SGNS_COLD_STORAGE)
addtest(cold_downgrade_live_smoke_test
    cold_downgrade_live_smoke_test.cpp
)

target_include_directories(cold_downgrade_live_smoke_test PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

target_link_libraries(cold_downgrade_live_smoke_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
    genius_node_test
    p2p::p2p_logger
    logger
    ipfs-lite-cpp::ipfs_datastore_in_memory
)

if(MSVC)
    target_link_options(cold_downgrade_live_smoke_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(cold_downgrade_live_smoke_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(cold_downgrade_live_smoke_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(cold_downgrade_live_smoke_test PROPERTIES LABELS "Tier3")
endif()

# Dev-gated live-stack smoke (D-08c, v1.2 Phase 14 plan 14-03): the REAL
# MinIO wire (production S3 client + real drainer transitions) passes the
# cold-durability gate. Env-gated opt-in via SGNS_BLOCKSTORE_TEST_MINIO_BIN
# (community build; loud labelled skip without it) — NEVER in the Tier-1
# CI lane: registered LABELS "Tier3" (the cold_storage_wallclock_soak
# env-gated convention; Pitfall 6 label parity holds).
if(SGNS_COLD_STORAGE)
addtest(cold_prune_live_smoke_test
    cold_prune_live_smoke_test.cpp
)

target_include_directories(cold_prune_live_smoke_test PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

target_link_libraries(cold_prune_live_smoke_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
    genius_node_test
    p2p::p2p_logger
    logger
)

if(MSVC)
    target_link_options(cold_prune_live_smoke_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(cold_prune_live_smoke_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(cold_prune_live_smoke_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(cold_prune_live_smoke_test PROPERTIES LABELS "Tier3")
endif()

# Dev-gated live SC3 leg (D-11b, v1.2 Phase 16 plan 16-02 Task 3,
# COMP-03): a real MinIO wire + a REAL GC disposal strands a real
# TxPruner cycle with the disposed block still fetchable from the
# remote. Env-gated opt-in via SGNS_BLOCKSTORE_TEST_MINIO_BIN (community
# build; loud labelled skip without it) — NEVER in the Tier-1 CI lane:
# registered LABELS "Tier3" (the cold_prune_live_smoke convention; Pitfall
# 6 label parity holds).
if(SGNS_COLD_STORAGE)
addtest(cold_compose_live_smoke_test
    cold_compose_live_smoke_test.cpp
)

target_include_directories(cold_compose_live_smoke_test PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

target_link_libraries(cold_compose_live_smoke_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
    genius_node_test
    p2p::p2p_logger
    logger
)

if(MSVC)
    target_link_options(cold_compose_live_smoke_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(cold_compose_live_smoke_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(cold_compose_live_smoke_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(cold_compose_live_smoke_test PROPERTIES LABELS "Tier3")
endif()
# s3d-unavailable soak: outage-then-recovery iteration soak + D-09
# teardown legs + peer cold-fetch budget (TEST-02 SC3, v1.1 Phase 12
# plan 12-02). Registered ONLY under SGNS_COLD_STORAGE: hermetic fakes
# via the installed fake headers. The wall-clock soak (Task 3) registers
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


set_tests_properties(cold_storage_wallclock_soak PROPERTIES LABELS "Tier3")
endif()

# Seal-growth probe harness (SEAL-01 / SC1-SC3, v1.2 Phase 17 plan
# 17-01): the reusable measurement instrument (D-08) - minimal-stack
# hot store, sanctioned seal seeding at knobbed scale, raw-iterator
# suffix-filter enumeration, flat JSON evidence (D-09). The bounded
# 10^6 floor leg runs UNGATED in the Tier-1 lane with derived floors
# (D-07); the dev-gated big legs arriving with 17-02 stay env-gated
# and skip loudly. Registered ONLY under SGNS_COLD_STORAGE (gating
# parity with every cold Tier-1 suite); same link set as
# cold_compose_matrix_test.
if(SGNS_COLD_STORAGE)
addtest(cold_seal_growth_test
    cold_seal_growth_test.cpp
)

target_include_directories(cold_seal_growth_test PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

target_link_libraries(cold_seal_growth_test
    sgns-blockstore::sgns-blockstore
    crdt_globaldb
    crdt_datastore
    rocksdb
    genius_node_test
    p2p::p2p_logger
    logger
)

if(MSVC)
    target_link_options(cold_seal_growth_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(cold_seal_growth_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(cold_seal_growth_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(cold_seal_growth_test PROPERTIES LABELS "Tier1")
endif()
