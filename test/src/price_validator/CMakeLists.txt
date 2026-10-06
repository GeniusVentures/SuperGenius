addtest(price_validator_test
    price_validator_test.cpp
)

# PriceValidator.hpp -> LocalPriceManager.hpp -> PriceFetchError.hpp pulls
# <HTTPTypes.hpp>: the AsyncIOManager include dir is required even though
# this suite opens zero sockets (same as price_manager_test).
target_include_directories(price_validator_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

# genius_node_test supplies TokenAmount.o (the cost-binding oracle) under
# static-archive pull semantics — plain link, no whole-archive. coinprices
# itself must NOT link genius_node* (CMake cycle, Phase 7 RESEARCH Pitfall 5).
target_link_libraries(price_validator_test
    coinprices
    genius_node_test
    Boost::headers
)
