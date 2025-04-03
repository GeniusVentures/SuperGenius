add_library(component_factory
    CComponentFactory.cpp
)

target_link_libraries(component_factory
    PUBLIC
    outcome
    Boost::headers
)

supergenius_install(component_factory)