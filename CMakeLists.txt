cmake_minimum_required(VERSION 3.16)
project(dns_resolver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_TESTS "Enable unit tests" OFF)
option(ENABLE_EXAMPLE "Enable example" ON)

# 查找依赖项
find_package(c-ares REQUIRED)
find_package(nlohmann_json REQUIRED)

# 添加源文件
add_library(dns_resolver STATIC
        src/BasicMetrics.cpp
        src/CaresQueryStrategy.cpp
        src/ConfigManager.cpp
        src/DNSResolver.cpp
        src/EventPublisher.cpp
        src/LRUCache.cpp
        src/PluginManager.cpp
)

if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
    target_compile_definitions(dns_resolver PUBLIC -DNOMINMAX)
    set_property(TARGET dns_resolver PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

if (MSVC)

endif ()

target_include_directories(dns_resolver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 链接c-ares
target_link_libraries(dns_resolver
        PUBLIC
        c-ares::cares
        nlohmann_json::nlohmann_json
)

if (ENABLE_EXAMPLE)
    add_subdirectory(examples)
endif ()

if (ENABLE_TESTS)
    #    find_package(GTest REQUIRED)
    #    add_subdirectory(unit_tests)
endif ()
