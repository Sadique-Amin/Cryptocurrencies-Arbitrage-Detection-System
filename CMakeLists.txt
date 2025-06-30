cmake_minimum_required(VERSION 3.15)
project(ArbiSim)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimize build speed
set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "/Od")

# Disable overly strict warnings for faster compilation
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4820 /wd4625 /wd4626 /wd5026 /wd5027 /wd4365 /wd5219 /wd4464 /wd5039")
    # Speed up compilation with multi-processor build
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    # Reduce template instantiation depth for faster compilation
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
endif()

# Find only essential packages
find_package(Threads REQUIRED)

# Try to find Boost quickly, but don't fail if not found
find_package(Boost QUIET COMPONENTS system)
if(Boost_FOUND)
    message(STATUS "Boost found - enabling full networking features")
    add_definitions(-DHAVE_BOOST)
else()
    message(STATUS "Boost not found - using simplified networking")
endif()

# Try to find OpenSSL quickly
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found - enabling SSL features")
    add_definitions(-DHAVE_OPENSSL)
else()
    message(STATUS "OpenSSL not found - using simplified features")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# NO MORE nlohmann/json - we'll create our own minimal parser if needed

# Create the main executable
add_executable(arbisim
    src/main.cpp
)

# Link only essential libraries
target_link_libraries(arbisim PRIVATE
    Threads::Threads
)

# Link optional libraries if found
if(Boost_FOUND)
    target_link_libraries(arbisim PRIVATE Boost::system)
    target_compile_definitions(arbisim PRIVATE HAVE_BOOST)
endif()

if(OpenSSL_FOUND)
    target_link_libraries(arbisim PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(arbisim PRIVATE HAVE_OPENSSL)
endif()

# Include directories for the target
target_include_directories(arbisim PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Platform-specific settings
if(WIN32)
    target_link_libraries(arbisim PRIVATE ws2_32 wsock32)
    target_compile_definitions(arbisim PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()

# Set output directory
set_target_properties(arbisim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Performance test (simplified)
add_executable(perf_test tests/performance_test.cpp)
target_link_libraries(perf_test PRIVATE Threads::Threads)
target_include_directories(perf_test PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Enable unity builds for much faster compilation
set_target_properties(arbisim PROPERTIES
    CXX_UNITY_BUILD ON
    CXX_UNITY_BUILD_BATCH_SIZE 2
)

set_target_properties(perf_test PROPERTIES
    CXX_UNITY_BUILD ON
    CXX_UNITY_BUILD_BATCH_SIZE 2
)

# Precompiled headers for even faster builds (VS 2019+)
if(MSVC AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)
    target_precompile_headers(arbisim PRIVATE
        <iostream>
        <vector>
        <string>
        <memory>
        <thread>
        <atomic>
        <chrono>
        <functional>
        <fstream>
        <iomanip>
    )
endif()

# Create directories
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/include)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/src)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/dashboard)

# Enable testing
enable_testing()
add_test(NAME PerformanceTest COMMAND perf_test)