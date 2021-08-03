

add_library(changes_tracker
    impl/storage_changes_tracker_impl.cpp
    impl/changes_trie.cpp
    )
set_target_properties(changes_tracker PROPERTIES PUBLIC_HEADER "changes_tracker.hpp;changes_trie_config.hpp;impl/storage_changes_tracker_impl.hpp;impl/changes_trie.hpp")
target_link_libraries(changes_tracker
    buffer
    blob
    logger
    scale
    )
supergenius_install(changes_tracker)
