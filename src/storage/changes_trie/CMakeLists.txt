

add_library(changes_tracker
    impl/storage_changes_tracker_impl.cpp
    impl/changes_trie.cpp
    )
target_link_libraries(changes_tracker
    buffer
    blob
    logger
    scale
    )
supergenius_install(changes_tracker)
