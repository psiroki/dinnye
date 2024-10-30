cmake_minimum_required(VERSION 3.8)

# Set the project name and version
project(planets VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find the SDL1 library
find_package(SDL REQUIRED)
find_package(SDL_ttf REQUIRED)

option(SPEEDTESTING "Start with a fair amount of fruits to test the speed reliably" ON)
option(BITTBOY "Target Bittboy" OFF)
option(FIXED "Use fixed point math" ${BITTBOY})
option(DEBUG_VISUALIZATION "Visualize internal workings of the renderer" OFF)

if(FIXED)
  message(STATUS "Fixed point math mode")
  add_definitions(-DFIXED)
endif()

if(SPEEDTESTING)
  add_definitions(-DSPEEDTESTING)
endif()

if(BITTBOY)
  message(STATUS "Building for BittBoy")
  add_definitions(-DBITTBOY)
endif()

if(DEBUG_VISUALIZATION)
  message(STATUS "Debug visualization on")
  add_definitions(-DDEBUG_VISUALIZATION)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()

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

# Link with libraries
target_link_libraries(planets SDL SDL_ttf m pthread)
