add_library(hexutil
    hexutil.hpp
    hexutil.cpp
)
target_link_libraries(hexutil
    PUBLIC
    outcome
)
supergenius_install(hexutil)

add_library(buffer
    buffer.hpp
    buffer.cpp
)
target_link_libraries(buffer
    PUBLIC
    Boost::headers
    outcome
    PRIVATE
    hexutil
)
supergenius_install(buffer)

add_library(blob
    blob.hpp
    blob.cpp
)
target_link_libraries(blob
    PUBLIC
    hexutil
    Boost::headers
)
supergenius_install(blob)

add_library(logger
    logger.hpp
    logger.cpp
)
target_link_libraries(logger
    PUBLIC
    spdlog::spdlog
)
supergenius_install(logger)

add_library(mp_utils
    mp_utils.cpp
    mp_utils.hpp
)
target_link_libraries(mp_utils
    PUBLIC
    Boost::headers
)
supergenius_install(mp_utils)

add_library(fixed_point
    fixed_point.cpp
    fixed_point.hpp
)
target_link_libraries(fixed_point
    PUBLIC
    Boost::headers
    outcome
)
supergenius_install(fixed_point)

set(SUPERGENIUS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(SUPERGENIUS_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(SUPERGENIUS_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(SUPERGENIUS_VERSION_PRE_RELEASE "alpha")  
set(SUPERGENIUS_VERSION_BUILD_METADATA "2025-02-25") 
set(SUPERGENIUS_VERSION_NUMBER "0x${SUPERGENIUS_VERSION_MAJOR}${SUPERGENIUS_VERSION_MINOR}${SUPERGENIUS_VERSION_PATCH}00")

message(STATUS "SUPERGENIUS_VERSION_MAJOR: ${SUPERGENIUS_VERSION_MAJOR}")
message(STATUS "SUPERGENIUS_VERSION_MINOR: ${SUPERGENIUS_VERSION_MINOR}")
message(STATUS "SUPERGENIUS_VERSION_PATCH: ${SUPERGENIUS_VERSION_PATCH}")
message(STATUS "SUPERGENIUS_VERSION_PRE_RELEASE: ${SUPERGENIUS_VERSION_PRE_RELEASE}")
message(STATUS "SUPERGENIUS_VERSION_BUILD_METADATA: ${SUPERGENIUS_VERSION_BUILD_METADATA}")
message(STATUS "SUPERGENIUS_VERSION_NUMBER: ${SUPERGENIUS_VERSION_NUMBER}")

message(STATUS "Generationg sgns_version.hpp from sgns_version.hpp.in")

cmake_policy(SET CMP0115 NEW)

set_source_files_properties(sgns_version.hpp.in PROPERTIES HEADER_FILE_ONLY TRUE)


configure_file(
    sgns_version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/../../../../../src/base/sgns_version.hpp
    @ONLY
)

# install_hfile( ${CMAKE_CURRENT_BINARY_DIR}/../../generated/base)

if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/sgns_version.hpp")
    message(STATUS "sgns_version.hpp generated")
else()
    message(WARNING "Arquivo sgns_version.hpp NÃO foi gerado!")
endif()

add_library(sgns_version
sgns_version.cpp
${CMAKE_CURRENT_BINARY_DIR}/../../generated/base/sgns_version.hpp
)
target_link_libraries(sgns_version
    PUBLIC
    Boost::headers
    outcome
)
supergenius_install(sgns_version)

