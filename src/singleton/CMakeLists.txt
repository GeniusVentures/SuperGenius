add_library(component_factory
    CComponentFactory.cpp
)

target_link_libraries(component_factory
    outcome
    Boost::boost)

supergenius_install(component_factory)