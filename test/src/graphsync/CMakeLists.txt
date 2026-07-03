addtest(pubsub_graphsync_test
    pubsub_graphsync_test.cpp
)

target_include_directories(pubsub_graphsync_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(pubsub_graphsync_test
    genius_node_test
	p2p::p2p_logger
	logger
)

if(MSVC)
    target_link_options(pubsub_graphsync_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(pubsub_graphsync_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(pubsub_graphsync_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()