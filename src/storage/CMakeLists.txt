

add_subdirectory(rocksdb)
add_subdirectory(trie)
add_subdirectory(in_memory)
add_subdirectory(changes_trie)

add_library(database_error
    database_error.cpp
    )
set_target_properties(database_error PROPERTIES PUBLIC_HEADER "database_error.hpp")
target_link_libraries(database_error
    Boost::boost
    outcome
    )
supergenius_install(database_error)
