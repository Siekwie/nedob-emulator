cmake_minimum_required(VERSION 3.16)
project(nedobEMU LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the output directory for built objects
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

# SDL3 include directory
set(SDL3_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/vendored/include")

# SDL3 library directory (pre-built libraries)
set(SDL3_LIB_DIR "${CMAKE_SOURCE_DIR}/vendored/SDL3")

# Create imported target for SDL3
add_library(SDL3::SDL3 SHARED IMPORTED)

# Set the location of the SDL3 library
if(WIN32)
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_LOCATION "${SDL3_LIB_DIR}/SDL3.dll"
        IMPORTED_IMPLIB "${SDL3_LIB_DIR}/SDL3.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL3_INCLUDE_DIR}"
    )
else()
    # For non-Windows platforms, adjust as needed
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_LOCATION "${SDL3_LIB_DIR}/libSDL3.so"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL3_INCLUDE_DIR}"
    )
endif()

# Find all .cpp and .hpp files in the src directory
file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.hpp")

# Create your game executable target (console application)
add_executable(NedobEMU ${SOURCES} ${HEADERS})

# Copy DLL to output directory (Windows only)
if(WIN32)
    add_custom_command(TARGET NedobEMU POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL3_LIB_DIR}/SDL3.dll"
        $<TARGET_FILE_DIR:NedobEMU>
    )
endif()

# Link to SDL3 library
target_link_libraries(NedobEMU PRIVATE SDL3::SDL3)

# Include SDL3 headers
target_include_directories(NedobEMU PRIVATE "${SDL3_INCLUDE_DIR}")