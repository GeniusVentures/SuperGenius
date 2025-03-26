
addtest(pubsub_counts_test
    pubsub_counts.cpp
    )

target_link_libraries(pubsub_counts_test
    logger
    Boost::headers
    p2p::p2p_logger
    ipfs-pubsub
)