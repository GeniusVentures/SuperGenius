# Header-only PeerRegistry abstraction (D-04). INTERFACE library: usage
# requirements only - INTERFACE libraries cannot take PUBLIC/PRIVATE linkage.
add_library(peerregistry INTERFACE)
target_include_directories(peerregistry INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(peerregistry INTERFACE securecrdt)
