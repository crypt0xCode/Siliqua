project ("Vetuscoin")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR}/include)
link_directories(${PROJECT_SOURCE_DIR}/libs)

file(GLOB_RECURSE SRC_FILES "src/*.cpp")
add_executable(${PROJECT_NAME} ${SRC_FILES}          "include/crypto/ecdsa.cpp" "include/crypto/ecdsa.h" "include/logger.h" "include/crypto/secp256k1context.h")

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/libs/libsecp256k1.lib
    ${PROJECT_SOURCE_DIR}/libs/cryptlib.lib
    bcrypt
)

target_compile_definitions(${PROJECT_NAME} PRIVATE SECP256K1_STATIC)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)