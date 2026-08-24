cmake_minimum_required(VERSION 3.28)

project(
    kv_store
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(kv_enable_warnings target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            ${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wshadow
                -Werror
        )
    elseif(MSVC)
        target_compile_options(
            ${target_name}
            PRIVATE
                /W4
                /WX
                /permissive-
        )
    endif()
endfunction()

add_library(
    kv_core
    STATIC
        src/version.cpp
)

add_library(kv::core ALIAS kv_core)

target_include_directories(
    kv_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

kv_enable_warnings(kv_core)

add_executable(
    kv-server
    apps/server/main.cpp
)

target_link_libraries(
    kv-server
    PRIVATE
        kv::core
)

kv_enable_warnings(kv-server)

include(CTest)

if(BUILD_TESTING)
    find_package(GTest 1.14 REQUIRED)
    include(GoogleTest)

    add_executable(
        kv_tests
        tests/version_test.cpp
    )

    target_link_libraries(
        kv_tests
        PRIVATE
            kv::core
            GTest::gtest_main
    )

    kv_enable_warnings(kv_tests)
    gtest_discover_tests(kv_tests)
endif()