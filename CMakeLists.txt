cmake_minimum_required(VERSION 3.10)
project(boww_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Sources ---
# Explicitly listing sources ensures CMake knows exactly what to compile
set(SOURCES
    src/main.cpp
    src/BoWWServer.cpp
    src/BoWWServer.h
    src/GroupController.cpp
    src/GroupController.h
    src/ClientSession.cpp
    src/ClientSession.h
    src/VADEngine.cpp
    src/VADEngine.h
    src/ConfigManager.cpp
    src/ConfigManager.h
    src/AudioOutputRouter.cpp
    src/AudioOutputRouter.h
    src/MDNSService.cpp
    src/MDNSService.h
    src/BoWWServerDefs.h
    src/SimpleAGC.h  # <--- Ensure this is included
)

# --- 1. Threading & Boost ---
find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

# --- 2. ALSA (Audio Hardware) ---
find_package(ALSA REQUIRED)
if(ALSA_FOUND)
    add_definitions(-D__LINUX_ALSA__)
    message(STATUS "ALSA Found: ${ALSA_LIBRARIES}")
endif()

# --- 3. Avahi (mDNS) - Manual Search ---
# Bypassing pkg-config because it failed on your Pi previously
find_path(AVAHI_CLIENT_INCLUDE_DIR avahi-client/client.h)
find_path(AVAHI_COMMON_INCLUDE_DIR avahi-common/watch.h)
find_library(AVAHI_CLIENT_LIB avahi-client)
find_library(AVAHI_COMMON_LIB avahi-common)

if (AVAHI_CLIENT_INCLUDE_DIR AND AVAHI_COMMON_INCLUDE_DIR AND AVAHI_CLIENT_LIB AND AVAHI_COMMON_LIB)
    message(STATUS "Found Avahi: ${AVAHI_CLIENT_LIB}")
    set(AVAHI_INCLUDE_DIRS ${AVAHI_CLIENT_INCLUDE_DIR} ${AVAHI_COMMON_INCLUDE_DIR})
    set(AVAHI_LIBRARIES ${AVAHI_CLIENT_LIB} ${AVAHI_COMMON_LIB})
else()
    message(FATAL_ERROR "Could not find Avahi libraries. Ensure libavahi-client-dev is installed.")
endif()

# --- 4. YAML-CPP ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAMLCPP REQUIRED yaml-cpp)

# --- 5. JSON ---
find_package(nlohmann_json REQUIRED)

# --- 6. ONNX Runtime (Local Libs) ---
# We look in the 'libs' folder created by your setup script
set(ONNX_ROOT "${CMAKE_SOURCE_DIR}/libs/onnxruntime")
set(ONNX_INCLUDE_DIR "${ONNX_ROOT}/include")
# The lib name might be libonnxruntime.so or libonnxruntime.so.1.16.3
# We search specifically for the .so file
file(GLOB ONNX_LIB "${ONNX_ROOT}/lib/libonnxruntime.so*")

if(NOT EXISTS "${ONNX_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "ONNX Headers not found at ${ONNX_INCLUDE_DIR}. Did you run setup_env_pi.sh?")
endif()

message(STATUS "Using Local ONNX Runtime: ${ONNX_ROOT}")

# --- Build Executable ---
add_executable(boww_server ${SOURCES})

# --- Includes ---
target_include_directories(boww_server PRIVATE 
    src
    ${AVAHI_INCLUDE_DIRS}
    ${YAMLCPP_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${ONNX_INCLUDE_DIR}
)

# --- Linking ---
target_link_libraries(boww_server PRIVATE
    Threads::Threads
    ${ALSA_LIBRARIES}
    ${AVAHI_LIBRARIES}
    ${YAMLCPP_LIBRARIES}
    nlohmann_json::nlohmann_json
    Boost::system
    Boost::thread
    ${ONNX_LIB} 
)

# --- Post-Build: Copy ONNX Lib ---
# This ensures the .so file is next to the executable so it runs without setting LD_LIBRARY_PATH
add_custom_command(TARGET boww_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${ONNX_ROOT}/lib
    ${CMAKE_BINARY_DIR}
    COMMENT "Copying ONNX Runtime libs to build directory..."
)
