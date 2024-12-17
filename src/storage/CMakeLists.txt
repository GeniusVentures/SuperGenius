add_subdirectory(rocksdb)
add_subdirectory(trie)
add_subdirectory(in_memory)
add_subdirectory(changes_trie)

add_library(database_error
    database_error.cpp
)
target_link_libraries(database_error
    PUBLIC
    Boost::headers
    outcome
)
supergenius_install(database_error)
