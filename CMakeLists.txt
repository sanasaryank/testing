cmake_minimum_required(VERSION 3.15)
project(http_client VERSION 1.0.0 LANGUAGES CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(CURL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(ZLIB REQUIRED)

# Add the library
add_library(http_client
    http_client.cpp
    http_client.h
)

# Include directories
target_include_directories(http_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CURL_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(http_client PUBLIC
    ${CURL_LIBRARIES}
    Boost::system
    ${ZLIB_LIBRARIES}
)

# Compiler warnings
if(MSVC)
    target_compile_options(http_client PRIVATE /W4)
else()
    target_compile_options(http_client PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Optional: Build example if available
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/example.cpp")
    add_executable(http_client_example example.cpp)
    target_link_libraries(http_client_example PRIVATE http_client)
endif()

# Installation rules
install(TARGETS http_client
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES http_client.h
    DESTINATION include
)
