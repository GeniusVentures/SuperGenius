add_subdirectory(account)
add_subdirectory(authorship)
add_subdirectory(base)
add_subdirectory(blockchain)
add_subdirectory(scale)
add_subdirectory(primitives)
add_subdirectory(crypto)
add_subdirectory(verification)
add_subdirectory(storage)
#add_subdirectory(runtime)
add_subdirectory(crdt)
add_subdirectory(processing)
add_subdirectory(watcher)
add_subdirectory(processing_nodes)
#add_subdirectory(processing_multi)
add_subdirectory(transaction_sync)
add_subdirectory(price_retrieval)
add_subdirectory(multiaccount)
if (BUILD_WITH_PROOFS)
    add_subdirectory(proof)
endif()
add_subdirectory(account_creation)
add_subdirectory(pubsub_counts)
add_subdirectory(graphsync)

