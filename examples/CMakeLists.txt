
add_executable(example_dns_resolver
        Examples.cpp
)

target_link_libraries(example_dns_resolver
        PRIVATE
        dns_resolver
)

if (MSVC)
    set_property(TARGET example_dns_resolver PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

add_executable(example2
        Examples2.cpp
)

target_link_libraries(example2
        PRIVATE
        dns_resolver
)

if (MSVC)
    set_property(TARGET example2 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()