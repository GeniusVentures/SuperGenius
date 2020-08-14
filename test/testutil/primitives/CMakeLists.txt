

add_library(testutil_primitives_generator
    mp_utils.hpp
    hash_creator.cpp
    )
target_link_libraries(testutil_primitives_generator
    blob
    )
