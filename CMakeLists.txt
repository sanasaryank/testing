cmake_minimum_required(VERSION 3.14)
project(http_client_implementations VERSION 1.0 LANGUAGES CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

# libcurl implementation library
add_library(http_client_curl
    http_client_curl.cpp
    http_client_curl.hpp
    http_common.hpp
)

target_include_directories(http_client_curl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(http_client_curl 
    PUBLIC 
        CURL::libcurl
)

# Boost.ASIO implementation library
add_library(http_client_asio
    http_client_asio.cpp
    http_client_asio.hpp
    http_common.hpp
)

target_include_directories(http_client_asio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(http_client_asio 
    PUBLIC 
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)

# Example executable
add_executable(example_usage
    example_usage.cpp
)

target_link_libraries(example_usage
    PRIVATE
        http_client_curl
        http_client_asio
)

# Compiler warnings
if(MSVC)
    target_compile_options(http_client_curl PRIVATE /W4)
    target_compile_options(http_client_asio PRIVATE /W4)
    target_compile_options(example_usage PRIVATE /W4)
else()
    target_compile_options(http_client_curl PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(http_client_asio PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(example_usage PRIVATE -Wall -Wextra -Wpedantic)
endif()
