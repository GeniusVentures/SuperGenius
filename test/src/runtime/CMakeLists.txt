addtest(wasm_memory_test
    wasm_memory_test.cpp
    )
target_link_libraries(wasm_memory_test
    binaryen_wasm_memory
    )