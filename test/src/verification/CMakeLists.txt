add_subdirectory(production)
add_subdirectory(finality)

addtest(production_lottery_test
    production_lottery_test.cpp
)
target_link_libraries(production_lottery_test
    production_lottery
    buffer
    mp_utils
)
