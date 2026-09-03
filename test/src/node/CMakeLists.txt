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

# Several suites in this directory boot multiple full nodes; running them
# concurrently starves CPU on smaller CI runners (observed: a genesis creator
# starved for 30s while eight test binaries ran), stalling startup past any
# sane timeout. Sharing this lock serializes them against the other
# node-booting suites while light suites keep running in parallel.
set_tests_properties(node_shutdown_race_test
    PROPERTIES RESOURCE_LOCK genius_node_pool)
