add_library(binaryen_wasm_memory
    wasm_memory_impl.hpp
    wasm_memory_impl.cpp
    )
target_link_libraries(binaryen_wasm_memory
    buffer
    binaryen::binaryen
    )
supergenius_install(binaryen_wasm_memory)

add_library(binaryen_wasm_module
    module/wasm_module_impl.cpp
    module/wasm_module_factory_impl.cpp
    )
target_link_libraries(binaryen_wasm_module
    binaryen::binaryen
    logger
    )
