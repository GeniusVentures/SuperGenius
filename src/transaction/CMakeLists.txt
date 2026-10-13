# Transaction coordination: TransactionManager and the consensus-facing half
# split out of it. The remaining transaction types still live in src/account
# and are expected to move here incrementally.
#
# This is an OBJECT library rather than a static one because the dependency
# with src/account is mutual: TransactionManager needs the transaction types
# and input validators compiled into genius_node_objs, while BridgeRelayer in
# that same target needs TransactionManager. Both object sets are aggregated
# into genius_node / genius_node_test, so no link-time cycle exists.
add_library(genius_transaction_objs OBJECT
    TransactionManager.cpp
    TransactionConsensusHandler.cpp
)

target_link_libraries(genius_transaction_objs ${GENIUS_NODE_LIBS})
target_include_directories(genius_transaction_objs PRIVATE ${AsyncIOManager_INCLUDE_DIR})

add_dependencies(genius_transaction_objs rlp)

if(MSVC AND NOT CLANG)
    target_compile_options(genius_transaction_objs PRIVATE /constexpr:steps1500000)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(genius_transaction_objs PRIVATE -fconstexpr-steps=10000000)
endif()
