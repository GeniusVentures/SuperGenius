

# add_subdirectory(production)

addtest(production_lottery_test
    production_lottery_test.cpp
    )
target_link_libraries(production_lottery_test
    production_lottery
    )
