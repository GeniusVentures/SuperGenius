print("${blocklattice_DIR}")
find_package(blocklattice CONFIG REQUIRED PATHS ${blocklattice_DIR} NO_DEFAULT_PATH)

add_executable (sgns_node
    entry.cpp)
target_link_libraries(sgns_node
    blocklattice::secure
    Boost::system
    )