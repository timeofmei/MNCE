cmake_minimum_required(VERSION 3.30.5)

project(MNCE LANGUAGES CXX)

include(CTest)

if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "The Windows build requires the MSVC toolchain.")
endif()

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Windows build requires an x64 toolchain environment.")
endif()

find_package(
    Qt6 6.10.2
    REQUIRED COMPONENTS
        Concurrent
        Core
        Multimedia
        Network
        Sql
        Widgets
)

add_library(mnce_project_options INTERFACE)
target_compile_features(mnce_project_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(
        mnce_project_options
        INTERFACE
            /W4
            /permissive-
            /utf-8
    )
endif()

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
