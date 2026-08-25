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

find_package(Threads REQUIRED)

option(
    KV_ENABLE_ASAN_UBSAN
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
)

option(
    KV_ENABLE_TSAN
    "Enable ThreadSanitizer"
    OFF
)

if(KV_ENABLE_ASAN_UBSAN AND KV_ENABLE_TSAN)
    message(
        FATAL_ERROR
        "AddressSanitizer and ThreadSanitizer cannot be enabled together"
    )
endif()

if(KV_ENABLE_ASAN_UBSAN OR KV_ENABLE_TSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(
            FATAL_ERROR
            "The configured sanitizers require GCC or Clang"
        )
    endif()
endif()

add_library(kv_project_options INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        kv_project_options
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Werror
    )
elseif(MSVC)
    target_compile_options(
        kv_project_options
        INTERFACE
            /W4
            /WX
            /permissive-
    )
endif()

if(KV_ENABLE_ASAN_UBSAN)
    target_compile_options(
        kv_project_options
        INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all
    )

    target_link_options(
        kv_project_options
        INTERFACE
            -fsanitize=address,undefined
    )
elseif(KV_ENABLE_TSAN)
    target_compile_options(
        kv_project_options
        INTERFACE
            -fsanitize=thread
            -fno-omit-frame-pointer
    )

    target_link_options(
        kv_project_options
        INTERFACE
            -fsanitize=thread
    )
endif()

add_library(
    kv_core
    STATIC
        src/kv_engine.cpp
        src/version.cpp
)

add_library(kv::core ALIAS kv_core)

target_include_directories(
    kv_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(
    kv_core
    PUBLIC
        Threads::Threads
    PRIVATE
        kv_project_options
)

add_executable(
    kv-server
    apps/server/main.cpp
)

target_link_libraries(
    kv-server
    PRIVATE
        kv::core
        kv_project_options
)

include(CTest)

if(BUILD_TESTING)
    find_package(GTest 1.14 REQUIRED)
    include(GoogleTest)

    add_executable(
        kv_tests
        tests/kv_engine_test.cpp
        tests/version_test.cpp
    )

    target_link_libraries(
        kv_tests
        PRIVATE
            kv::core
            kv_project_options
            GTest::gtest_main
            Threads::Threads
    )

    gtest_discover_tests(kv_tests)
endif()