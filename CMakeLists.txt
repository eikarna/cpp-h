cmake_minimum_required(VERSION 3.10)
project(CppHServer)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Define the source files
set(SOURCES server.cpp)

# Find OpenSSL library
find_package(OpenSSL REQUIRED)

# Define the executable
add_executable(server ${SOURCES})

# Link the OpenSSL libraries
target_link_libraries(server OpenSSL::SSL OpenSSL::Crypto)

# Set compiler options
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(server PRIVATE -O2)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(server PRIVATE -g)
endif()
