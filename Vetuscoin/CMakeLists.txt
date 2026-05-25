cmake_minimum_required(VERSION 3.15)
project("Vetuscoin")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Cross-platform libs search
# ============================================================================

find_package(PkgConfig QUIET)

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

add_executable(${PROJECT_NAME} ${SRC_FILES} "include/core/block.h")

# ============================================================================
# Compile (Windows: UTF-8)
# ============================================================================

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    # Optionally disable warnings for external headers
    target_compile_options(${PROJECT_NAME} PRIVATE /W0)
endif()

# ============================================================================
# Libs linking
# ============================================================================

target_link_libraries(${PROJECT_NAME} PRIVATE
        ${SECP256K1_LIB}
        ${CRYPTOPP_LIB}
)

# bcrypt only for Windows
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE bcrypt)
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