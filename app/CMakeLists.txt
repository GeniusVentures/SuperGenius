add_executable (sgns_demo
    runner.cpp
    SuperGeniusDemoApp.cpp
    )

  target_link_libraries(sgns_demo
      logger
      Boost::system
      Boost::thread
      Boost::filesystem
      ipfs_blockservice
      #block_producing_node_application
      validating_node_application
      #app_config_impl
      #protobuf::libprotobuf
      #${USER_ENV_LIBRARY}  # On windows platform , sr25519 library needs extra library userenv, so folllowing code is added
      #${WIN_CRYPT_LIBRARY}
      )
  if(FORCE_MULTILE)
    set_target_properties(sgns_demo PROPERTIES LINK_FLAGS ${MULTIPLE_OPTION})
  endif()

  supergenius_install(sgns_demo)
