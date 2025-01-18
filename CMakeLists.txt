cmake_minimum_required(VERSION 3.8)

include(FindPkgConfig)

# Set the project name and version
project(planets VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(BITTBOY "Target Bittboy" OFF)
option(RGNANO "Target RG Nano" OFF)
option(MIYOOA30 "Target Miyoo A30" OFF)
option(MIYOO "Target Miyoo Mini (Plus)" OFF)
option(PORTMASTER "Target PortMaster" OFF)
option(RG35XX22 "Target RG35XX22 Garlic" OFF)

if(MIYOOA30 OR PORTMASTER)
  set(USE_SDL2_DEFAULT ON)
  set(USE_GAME_CONTROLLER_DEFAULT ${PORTMASTER})
else()
  set(USE_SDL2_DEFAULT OFF)
  set(USE_GAME_CONTROLLER_DEFAULT OFF)
endif()

if(BITTBOY OR RGNANO)
  set(LOREZ_DEFAULT ON)
  set(FIXED_DEFAULT ON)
else()
  set(LOREZ_DEFAULT OFF)
  set(FIXED_DEFAULT OFF)
endif()

option(FIXED "Use fixed point math" ${FIXED_DEFAULT})
option(LOREZ "Low resolution test" ${LOREZ_DEFAULT})
option(WIN_BUILD "Windows build" OFF)
option(SPEEDTESTING "Start with a fair amount of fruits to test the speed reliably" OFF)
option(DEBUG_VISUALIZATION "Visualize internal workings of the renderer" OFF)
option(USE_SDL2 "Use SDL2" ${USE_SDL2_DEFAULT})
option(USE_GAME_CONTROLLER "Use game controller API instead of internal mapping" ${USE_GAME_CONTROLLER_DEFAULT})

if(USE_SDL2)
  message(STATUS "Using SDL2")
  add_definitions(-DUSE_SDL2)
  find_package(SDL2 REQUIRED)
  pkg_search_module(SDL2TTF REQUIRED SDL2_ttf>=2.0.0)
else()
  find_package(SDL REQUIRED)
  if(NOT BITTBOY AND NOT RG35XX22)
    find_package(SDL_ttf REQUIRED)
  endif()
endif()

if(WIN_BUILD)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-mwindows)
  endif()
endif()

if(LOREZ)
  message(STATUS "Use low resolution textures")
  add_definitions(-DLOREZ)
endif()

if(FIXED)
  message(STATUS "Fixed point math mode")
  add_definitions(-DFIXED)
endif()

if(USE_GAME_CONTROLLER)
  message(STATUS "Using game controller API")
  add_definitions(-DUSE_GAME_CONTROLLER)
endif()

if(SPEEDTESTING)
  add_definitions(-DSPEEDTESTING)
endif()

if(DEBUG_VISUALIZATION)
  message(STATUS "Debug visualization on")
  add_definitions(-DDEBUG_VISUALIZATION)
endif()

if(BITTBOY)
  message(STATUS "Building for BittBoy")
  add_definitions(-DBITTBOY)
elseif(RG35XX22)
  message(STATUS "Building for RG35XX22 Garlic")
  add_definitions(-DRG35XX22 -DUSE_JOYSTICK)
elseif(PORTMASTER)
  message(STATUS "Building for PortMaster")
  add_definitions(-DPORTMASTER -DUSE_JOYSTICK)
elseif(MIYOOA30)
  message(STATUS "Building for Miyoo A30")
  add_definitions(-DMIYOOA30)
elseif(MIYOO)
  message(STATUS "Building for Miyoo Mini (Plus)")
  add_definitions(-DMIYOO -DMIYOO_AUDIO)
elseif(RGNANO)
  message(STATUS "Building for RG Nano")
  add_definitions(-DRGNANO)
else()
  add_definitions(-DDESKTOP)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined,address")
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

if(USE_SDL2)
  target_link_libraries(planets SDL2 SDL2_ttf m pthread)
else()
  target_link_libraries(planets SDL SDL_ttf m pthread)
endif()

if(MIYOO)
  target_link_options(planets PRIVATE -lmi_ao -lcam_os_wrapper -lmi_sys)
endif()

