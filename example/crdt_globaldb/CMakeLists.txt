add_executable(crdt_globaldb_app globaldb_app.cpp)
target_link_libraries(crdt_globaldb_app 
    crdt_globaldb
    Boost::program_options
    )

# Compatibility target used by the production GlobalDB composition verification.
add_custom_target(globaldb_app DEPENDS crdt_globaldb_app)
