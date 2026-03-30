cmake_minimum_required(VERSION 3.16)
project(nedobEMU LANGUAGES CXX C)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the output directory for built objects
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

# This assumes SDL source is available in vendored/SDL
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)

# Find all .cpp and .hpp files in the src directory
file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.hpp")

# AES for NCCH decryption (C)
list(APPEND SOURCES "${CMAKE_SOURCE_DIR}/vendored/tiny_aes/aes.c")

# Create your game executable target (console application)
add_executable(NedobEMU ${SOURCES} ${HEADERS})

# Link to SDL3 library
target_link_libraries(NedobEMU PRIVATE SDL3::SDL3)
