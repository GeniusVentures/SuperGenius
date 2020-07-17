add_library(block_tree_error
    block_tree_error.hpp
    impl/block_tree_error.cpp
    )
target_link_libraries(block_tree_error
    outcome
    )