

add_library(twox twox.cpp)
set_target_properties(twox PROPERTIES PUBLIC_HEADER "twox.hpp")
target_link_libraries(twox
  xxhash
    blob
  )
supergenius_install(twox)
