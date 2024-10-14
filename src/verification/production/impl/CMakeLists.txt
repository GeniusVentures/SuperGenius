

add_library(production_lottery
    production_lottery_impl.cpp
    production_lottery_impl.hpp
    )
target_link_libraries(production_lottery
    hasher
    vrf_provider
    logger
    )

add_library(threshold_util
    threshold_util.cpp
    )
target_link_libraries(threshold_util
    primitives
    sr25519_types
)

add_library(production_digests_util
    production_digests_util.cpp
    )
target_link_libraries(production_digests_util
    primitives
    scale
    sr25519_types
)

add_library(block_executor
    block_executor.cpp
    )
target_link_libraries(block_executor
    logger
    primitives
    scale
    block_tree_error
    threshold_util
    transaction_pool_error
    )

add_library(production
    production_impl.cpp
    production_impl.hpp
    )
target_link_libraries(production
    production_error
    Boost::boost
    sr25519-donna
    buffer
    logger
    scale
    blob
    outcome
    p2p::p2p_peer_id
    production_digests_util
    threshold_util
    block_executor
    )

add_library(production_synchronizer
    production_synchronizer_impl.cpp
    )
target_link_libraries(production_synchronizer
    logger
    primitives
    )

add_library(syncing_production_observer
    syncing_production_observer.cpp
    )
target_link_libraries(syncing_production_observer
    logger
    primitives
    sr25519_types
)
