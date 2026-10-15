cmake_minimum_required(VERSION 3.16)
project(BioGrid LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(BIOGRID_BUILD_GAME "Build the SFML game" ON)
option(BIOGRID_BUILD_TESTS "Build the headless foraging test" ON)

add_library(biogrid_core INTERFACE)
target_include_directories(biogrid_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(biogrid_core INTERFACE /W4)
else()
    target_compile_options(biogrid_core INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(BIOGRID_BUILD_GAME)
    find_package(SFML 2.5 COMPONENTS graphics window system QUIET)
    if(NOT SFML_FOUND)
        message(STATUS "SFML 2.x not found on the system, fetching 2.6.1")
        include(FetchContent)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        if(MSVC)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            set(SFML_USE_STATIC_STD_LIBS ON CACHE BOOL "" FORCE)
        endif()
        set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
        set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(sfml
            GIT_REPOSITORY https://github.com/SFML/SFML.git
            GIT_TAG 2.6.1
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(sfml)
    endif()

    add_executable(biogrid src/main.cpp)
    target_link_libraries(biogrid PRIVATE biogrid_core sfml-graphics sfml-window sfml-system)
    target_compile_definitions(biogrid PRIVATE
        BIOGRID_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        BIOGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    if(WIN32 AND TARGET sfml-main)
        set_target_properties(biogrid PROPERTIES WIN32_EXECUTABLE ON)
        target_link_libraries(biogrid PRIVATE sfml-main)
    endif()
endif()

if(BIOGRID_BUILD_TESTS)
    enable_testing()
    add_executable(forage_test tests/forage_test.cpp)
    target_link_libraries(forage_test PRIVATE biogrid_core)
    target_compile_definitions(forage_test PRIVATE BIOGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME forage COMMAND forage_test)

    find_package(Threads REQUIRED)
    add_executable(run_experiments experiments/run_experiments.cpp)
    target_link_libraries(run_experiments PRIVATE biogrid_core Threads::Threads)
    target_compile_definitions(run_experiments PRIVATE BIOGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()
