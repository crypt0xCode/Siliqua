cmake_minimum_required(VERSION 3.15)
project("Siliqua")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Single-config generators (Makefiles, Ninja) build unoptimized "" by default unless a build
# type is given explicitly - default to Release so a plain `cmake .. && cmake --build .` still
# produces an optimized binary, which is what matters on weak/old hardware.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ============================================================================
# Cross-platform libs search
# ============================================================================

find_package(PkgConfig QUIET)

# network/daemon.h uses std::thread - on Linux this needs an explicit -lpthread; Threads::Threads
# resolves that per-platform instead of hardcoding a flag that would not apply everywhere.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# libsecp256k1
if(WIN32)
    set(SECP256K1_LIB ${PROJECT_SOURCE_DIR}/libs/libsecp256k1.lib)
    set(SECP256K1_INCLUDE ${PROJECT_SOURCE_DIR}/libs)
else()
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SECP256K1 REQUIRED libsecp256k1)
        set(SECP256K1_LIB ${SECP256K1_LIBRARIES})
        set(SECP256K1_INCLUDE ${SECP256K1_INCLUDE_DIRS})
    else()
        find_library(SECP256K1_LIB secp256k1)
        find_path(SECP256K1_INCLUDE secp256k1.h)
    endif()

    if(NOT SECP256K1_LIB)
        message(FATAL_ERROR "libsecp256k1 not found. Install with: brew install secp256k1 (macOS) or apt-get install libsecp256k1-dev (Linux)")
    endif()
endif()

# Crypto++ (for RIPEMD-160)
if(WIN32)
    set(CRYPTOPP_LIB ${PROJECT_SOURCE_DIR}/libs/cryptlib.lib)
    set(CRYPTOPP_INCLUDE ${PROJECT_SOURCE_DIR}/libs)
else()
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(CRYPTOPP REQUIRED libcryptopp)
        set(CRYPTOPP_LIB ${CRYPTOPP_LIBRARIES})
        set(CRYPTOPP_INCLUDE ${CRYPTOPP_INCLUDE_DIRS})
    else()
        find_library(CRYPTOPP_LIB cryptopp)
        find_path(CRYPTOPP_INCLUDE cryptopp/ripemd.h)
    endif()

    if(NOT CRYPTOPP_LIB)
        message(FATAL_ERROR "libcryptopp not found. Install with: brew install cryptopp (macOS) or apt-get install libcrypto++-dev (Linux)")
    endif()
endif()

# ============================================================================
# Include path
# ============================================================================

include_directories(
        ${PROJECT_SOURCE_DIR}/include
        ${SECP256K1_INCLUDE}
        ${CRYPTOPP_INCLUDE}
)

if(WIN32)
    link_directories(${PROJECT_SOURCE_DIR}/libs)
endif()

# ============================================================================
# Sources
# ============================================================================

file(GLOB_RECURSE SRC_FILES "src/*.cpp")

# Collect own .cpp files from include (excluding any cryptopp sources)
file(GLOB_RECURSE INCLUDE_CPP_FILES "include/*.cpp")
# Remove all cryptopp .cpp files regardless of platform
list(FILTER INCLUDE_CPP_FILES EXCLUDE REGEX ".*/cryptopp/.*\\.cpp$")
list(APPEND SRC_FILES ${INCLUDE_CPP_FILES})

add_executable(${PROJECT_NAME} ${SRC_FILES} "include/core/block.h"
        include/consensus/pow.h
        include/storage/storage.h
        include/wallet/wallet.h
        include/network/socket.h
        include/network/message.h
        include/network/peer.h
        include/network/node.h
        include/network/daemon.h)

# ============================================================================
# Compile (Windows: UTF-8)
# ============================================================================

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    # Optionally disable warnings for external headers
    target_compile_options(${PROJECT_NAME} PRIVATE /W0)
    # MSVC's own Release default is already /O2 - nothing to override here.
else()
    # CMake's own GCC/Clang Release default is -O3, which favors aggressive inlining and
    # auto-vectorization (including instruction sets an old CPU may not have). -O2 is the
    # more conservative, widely-recommended choice for old/weak hardware: still well optimized,
    # smaller code, no risk of targeting instructions the target CPU does not support.
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:-O2>)
endif()

# ============================================================================
# Libs linking
# ============================================================================

target_link_libraries(${PROJECT_NAME} PRIVATE
        ${SECP256K1_LIB}
        ${CRYPTOPP_LIB}
        Threads::Threads
)

# bcrypt (fill_random) and ws2_32 (Winsock, network/socket.h) only for Windows
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE bcrypt ws2_32)
endif()

# libsecp256k1 static build definition for Windows
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SECP256K1_STATIC)
endif()

# ============================================================================
# Target directory
# ============================================================================

set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)