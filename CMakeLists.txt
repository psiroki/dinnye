cmake_minimum_required(VERSION 3.8)

# Set the project name and version
project(planets VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find the SDL1 library
find_package(SDL REQUIRED)

# Define directories containing source code
set(SRC_COMMON_DIR src/common)
set(SRC_NATIVE_DIR src/native)

# Collect all .cc source files from src/common and src/native
file(GLOB COMMON_SOURCES
    ${SRC_COMMON_DIR}/*.cc
)

file(GLOB NATIVE_SOURCES
    ${SRC_NATIVE_DIR}/*.cc
)

# Combine all source files into one list
set(SOURCES ${COMMON_SOURCES} ${NATIVE_SOURCES})

# Collect all .hh header files (optional, for IDE visibility)
file(GLOB COMMON_HEADERS
    ${SRC_COMMON_DIR}/*.hh
)

file(GLOB NATIVE_HEADERS
    ${SRC_NATIVE_DIR}/*.hh
)

# Combine all header files into one list
set(HEADERS ${COMMON_HEADERS} ${NATIVE_HEADERS})

# Include the directories for header files
include_directories(${SRC_COMMON_DIR} ${SRC_NATIVE_DIR})

# Add the executable target
add_executable(planets ${SOURCES} ${HEADERS})

# Link SDL1 and the math library (libm)
target_link_libraries(planets SDL m)
