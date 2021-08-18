add_executable(crdt_globaldb_app globaldb_app.cpp)
target_link_libraries(crdt_globaldb_app 
    crdt_globaldb
    Boost::program_options
    )
