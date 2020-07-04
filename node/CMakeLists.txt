print("${blocklattice_INCLUDE_DIR}")

add_executable (sgns_node
    entry.cpp
    app_delegate.cpp
    node.cpp
    )

target_link_libraries(sgns_node
    blocklattice::secure
    Boost::system
    )