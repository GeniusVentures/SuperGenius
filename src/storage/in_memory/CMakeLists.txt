

add_library(in_memory_storage
    in_memory_storage.cpp
    )
set_target_properties(in_memory_storage PROPERTIES PUBLIC_HEADER "in_memory_storage.hpp;in_memory_batch.hpp")
target_link_libraries(in_memory_storage
    buffer
    database_error
    )
supergenius_install(in_memory_storage)
