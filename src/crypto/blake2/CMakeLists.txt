

add_library(blake2
  blake2s.c
  blake2b.c
  )
set_target_properties(blake2 PROPERTIES PUBLIC_HEADER "blake2s.h;blake2b.h")
supergenius_install(blake2)
