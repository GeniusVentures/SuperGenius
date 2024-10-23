add_executable(node_example
    NodeExample.cpp
    )
    include_directories(
        ${PROJECT_SOURCE_DIR}/src
        )
    target_link_libraries(node_example PRIVATE
    sgns_account
    )