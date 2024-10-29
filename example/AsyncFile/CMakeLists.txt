# add_subdirectory(application)
add_executable(MNNExample MNNExample.cpp)
target_link_libraries(
    MNNExample
    PRIVATE
    AsyncIOManager
    Boost::boost
    p2p::p2p_cares
    p2p::p2p_router
)
target_include_directories(
    MNNExample
    PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)
include_directories(../include)
