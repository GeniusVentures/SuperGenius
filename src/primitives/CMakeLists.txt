

add_library(primitives
    author_api_primitives.hpp
    block.hpp
    block_header.hpp
    block_id.hpp
    common.hpp
    check_inherents_result.hpp
    digest.hpp
    extrinsic.hpp
    inherent_data.cpp
    inherent_data.hpp
    parachain_host.hpp
    scheduled_change.hpp
    transaction_validity.hpp
    transaction_validity.cpp
    version.hpp
    )
set_target_properties(primitives PROPERTIES PUBLIC_HEADER "author_api_primitives.hpp;block.hpp;block_header.hpp;block_id.hpp;common.hpp;check_inherents_result.hpp;digest.hpp;extrinsic.hpp;inherent_data.hpp;parachain_host.hpp;scheduled_change.hpp;transaction_validity.hpp;version.hpp")
target_link_libraries(primitives
    buffer
    scale
    blob
    )
supergenius_install(primitives)
