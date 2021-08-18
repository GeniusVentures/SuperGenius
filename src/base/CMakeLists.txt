add_library(hexutil
    hexutil.hpp
    hexutil.cpp
    )
set_target_properties(hexutil PROPERTIES PUBLIC_HEADER "hexutil.hpp")
target_link_libraries(hexutil
    Boost::boost
    outcome
    )
supergenius_install(hexutil)

add_library(buffer
    buffer.hpp
    buffer.cpp
    )
set_target_properties(buffer PROPERTIES PUBLIC_HEADER "buffer.hpp")
target_link_libraries(buffer
    hexutil
    )
supergenius_install(buffer)

add_library(blob
    blob.hpp
    blob.cpp
    )
set_target_properties(blob PROPERTIES PUBLIC_HEADER "blob.hpp")
target_link_libraries(blob
    hexutil
    )
supergenius_install(blob)


add_library(logger
    logger.cpp
    )
set_target_properties(logger PROPERTIES PUBLIC_HEADER "logger.hpp")
target_link_libraries(logger
    spdlog::spdlog
    )
supergenius_install(logger)

add_library(mp_utils
    mp_utils.cpp
    mp_utils.hpp
    )
set_target_properties(mp_utils PROPERTIES PUBLIC_HEADER "mp_utils.hpp")
target_link_libraries(mp_utils
    Boost::boost
    )
supergenius_install(mp_utils)
