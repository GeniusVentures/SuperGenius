

add_subdirectory(impl)

add_library(production_error
    production_error.cpp
    production_error.hpp
    )
target_link_libraries(production_error
    outcome
    )

add_library(epoch_storage
    impl/epoch_storage_impl.cpp
    )
target_link_libraries(epoch_storage
    sr25519_types
    )
