addtest(node_initialization_progress
    node_initialization_progress.cpp
)

target_link_libraries(node_initialization_progress
    genius_node
    rapidjson
)

addtest(node_shutdown_race_test
    node_shutdown_race_test.cpp
)

target_link_libraries(node_shutdown_race_test
    genius_node
    rapidjson
)
