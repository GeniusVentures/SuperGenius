addtest(pubsub_graphsync_test
    pubsub_graphsync_test.cpp
)

target_include_directories(pubsub_graphsync_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(pubsub_graphsync_test
    sgns_account
	p2p::p2p_logger
	logger
)

if(MSVC)
    target_link_options(pubsub_graphsync_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:sgns_account>)
elseif(APPLE)
    target_link_options(pubsub_graphsync_test PUBLIC -force_load "$<TARGET_FILE:sgns_account>")
else()
    target_link_options(pubsub_graphsync_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()