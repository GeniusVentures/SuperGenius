# libFuzzer+ASan harnesses. Only entered when SGNS_FUZZING=ON
# (test/CMakeLists.txt gates the add_subdirectory call itself).

addfuzztarget(fuzz_decode_log_data fuzz_decode_log_data.cpp)
target_link_libraries(fuzz_decode_log_data genius_node_test)
target_include_directories(fuzz_decode_log_data PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/evmrelay/include
)

addfuzztarget(fuzz_parse_burn_event_values fuzz_parse_burn_event_values.cpp)
target_link_libraries(fuzz_parse_burn_event_values genius_node_test)
target_include_directories(fuzz_parse_burn_event_values PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/evmrelay/include
)

addfuzztarget(fuzz_mint_transaction_deserialize fuzz_mint_transaction_deserialize.cpp)
target_link_libraries(fuzz_mint_transaction_deserialize genius_node_test)
target_include_directories(fuzz_mint_transaction_deserialize PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/evmrelay/include
)

add_test(
    NAME fuzz_decode_log_data_smoke
    COMMAND $<TARGET_FILE:fuzz_decode_log_data> -max_total_time=60
        ${CMAKE_CURRENT_SOURCE_DIR}/corpus/decode_log_data
)
add_test(
    NAME fuzz_parse_burn_event_values_smoke
    COMMAND $<TARGET_FILE:fuzz_parse_burn_event_values> -max_total_time=60
        ${CMAKE_CURRENT_SOURCE_DIR}/corpus/parse_burn_event_values
)
add_test(
    NAME fuzz_mint_transaction_deserialize_smoke
    COMMAND $<TARGET_FILE:fuzz_mint_transaction_deserialize> -max_total_time=60
        ${CMAKE_CURRENT_SOURCE_DIR}/corpus/mint_transaction_deserialize
)
