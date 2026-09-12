include_guard(GLOBAL)
get_filename_component(CONFIGURATION_APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_vendor "${CONFIGURATION_APP_ROOT}/Middlewares/Third_Party")

# Use a pinned external test-only dependency; never download into Middlewares.
include(FetchContent)
FetchContent_Declare(unity
    URL https://codeload.github.com/ThrowTheSwitch/Unity/tar.gz/refs/tags/v2.7.0
    URL_HASH SHA256=e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(unity)

# Compile the repository's real crypto, X.509 and TLS implementations read-only.
file(GLOB _mbedtls_sources CONFIGURE_DEPENDS "${_vendor}/mbedTLS/library/*.c")
add_library(configuration_test_mbedtls STATIC ${_mbedtls_sources})
target_include_directories(configuration_test_mbedtls PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include" "${_vendor}/mbedTLS/include"
    "${CONFIGURATION_APP_ROOT}/MBEDTLS/App")
target_compile_definitions(configuration_test_mbedtls PUBLIC
    MBEDTLS_CONFIG_FILE="configuration_test_mbedtls_config.h")
if(WIN32)
    target_link_libraries(configuration_test_mbedtls PUBLIC bcrypt)
endif()

# Only the networking platform is adapted; IPv4 byte order/netmask code is real lwIP.
add_library(configuration_test_lwip STATIC
    "${_vendor}/LwIP/src/core/ipv4/ip4_addr.c" "${_vendor}/LwIP/src/core/def.c")
target_include_directories(configuration_test_lwip PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include" "${_vendor}/LwIP/src/include")

add_library(configuration_model STATIC
    "${CONFIGURATION_APP_ROOT}/Configuration/src/configuration.c"
    "${CONFIGURATION_APP_ROOT}/Configuration/src/configuration_binary_codec.c")
target_include_directories(configuration_model PUBLIC "${CONFIGURATION_APP_ROOT}/Configuration/include")
target_link_libraries(configuration_model PUBLIC configuration_test_lwip configuration_test_mbedtls)
target_compile_features(configuration_model PUBLIC c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(configuration_model PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

# Embed fixed, independently produced fixtures without requiring run-time files or Python.
function(configuration_embed_fixture name path expected_size)
    file(READ "${path}" fixture_hex HEX)
    string(LENGTH "${fixture_hex}" fixture_hex_length)
    math(EXPR fixture_size "${fixture_hex_length} / 2")
    if(NOT fixture_size EQUAL expected_size)
        message(FATAL_ERROR "${path}: expected ${expected_size} bytes, got ${fixture_size}")
    endif()
    string(REGEX REPLACE "(..)" "0x\\1," fixture_c "${fixture_hex}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}.inc" "${fixture_c}\n")
endfunction()
configuration_embed_fixture(default_v2 "${CMAKE_CURRENT_LIST_DIR}/fixtures/default_v2.bin" 48)
configuration_embed_fixture(maximum_v2 "${CMAKE_CURRENT_LIST_DIR}/fixtures/maximum_v2.bin" 8475)
configuration_embed_fixture(root_ca "${CMAKE_CURRENT_LIST_DIR}/fixtures/root_ca_4096.pem" 4096)
add_library(configuration_test_fixtures STATIC configuration_test_fixtures.c)
target_include_directories(configuration_test_fixtures PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(configuration_test_fixtures PUBLIC configuration_model)
