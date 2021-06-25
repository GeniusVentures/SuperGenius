### supergenius_install should be called right after add_library(target)
function(supergenius_install target)
    install(TARGETS ${target} EXPORT supergeniusTargets
        LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME       DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FRAMEWORK     DESTINATION ${CMAKE_INSTALL_PREFIX}
        BUNDLE        DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
endfunction()

### workaround for imported libraries
function(supergenius_install_mini target)
    install(TARGETS ${target} EXPORT supergeniusTargets
        LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
endfunction()

function(supergenius_install_setup)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs HEADER_DIRS)
    cmake_parse_arguments(arg "${options}" "${oneValueArgs}"
        "${multiValueArgs}" ${ARGN})

    foreach (DIR IN ITEMS ${arg_HEADER_DIRS})
        get_filename_component(FULL_PATH ${DIR} ABSOLUTE)
        string(REPLACE ${CMAKE_CURRENT_SOURCE_DIR}/src "." RELATIVE_PATH ${FULL_PATH})
        get_filename_component(INSTALL_PREFIX ${RELATIVE_PATH} DIRECTORY)
        install(DIRECTORY ${DIR}
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${INSTALL_PREFIX}
            FILES_MATCHING PATTERN "*.hpp")
    endforeach ()

    install(
        EXPORT supergeniusTargets
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/supergenius
        NAMESPACE sgns::
    )
    export(
        EXPORT supergeniusTargets
        FILE ${CMAKE_CURRENT_BINARY_DIR}/supergeniusTargets.cmake
        NAMESPACE sgns::
    )

endfunction()
