addtest(price_retrieval_test
    price_retrieval_test.cpp
)

# Heavyweight shape (account-suite precedent): driving GeniusNode::GetCoinprice
# requires a full node — genius_node_test + json_secure_storage + the stub
# (price_test_support). addtest provides gtest_main, xunit XML, TIMEOUT 600,
# test_bin output, Vulkan DLL copy, clang-tidy disable.
target_link_libraries(price_retrieval_test genius_node_test json_secure_storage price_test_support)

if(MSVC)
    target_link_options(price_retrieval_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(price_retrieval_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(price_retrieval_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()
