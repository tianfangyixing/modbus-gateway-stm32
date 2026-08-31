cmake_minimum_required(VERSION 3.20)

project(modbus_gateway_host_tests C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(LWIP_ROOT "${REPOSITORY_ROOT}/Middlewares/Third_Party/LwIP")
set(MBEDTLS_ROOT "${REPOSITORY_ROOT}/Middlewares/Third_Party/mbedTLS")

add_library(unity STATIC
    third_party/unity/unity.c
)

target_include_directories(unity PUBLIC
    third_party/unity
)

add_library(management_frame_runtime STATIC
    "${REPOSITORY_ROOT}/Management/src/management_frame.c"
)

target_compile_options(management_frame_runtime PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_include_directories(management_frame_runtime PUBLIC
    "${REPOSITORY_ROOT}/Management/include"
)

add_executable(management_frame_tests
    Management/test_management_frame.c
)

target_compile_options(management_frame_tests PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_link_libraries(management_frame_tests PRIVATE
    unity
    management_frame_runtime
)

add_test(NAME management_frame_tests COMMAND management_frame_tests)
set_tests_properties(management_frame_tests PROPERTIES LABELS "management;frame")

add_library(management_transport_runtime STATIC
    "${REPOSITORY_ROOT}/Management/src/management_transport.c"
)

target_compile_definitions(management_transport_runtime PRIVATE
    MANAGEMENT_TRANSPORT_TEST
)

target_compile_options(management_transport_runtime PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_include_directories(management_transport_runtime PRIVATE
    support/management
    support
    "${REPOSITORY_ROOT}/Management/include"
    "${REPOSITORY_ROOT}/Configuration/include"
    "${REPOSITORY_ROOT}/MQTT/include"
    "${REPOSITORY_ROOT}/SNTP/include"
    "${LWIP_ROOT}/src/include"
    "${LWIP_ROOT}/system"
)

target_link_libraries(management_transport_runtime PUBLIC
    management_frame_runtime
)

add_library(management_transport_test_adapter STATIC
    support/management/management_transport_test_adapter.c
)

target_compile_options(management_transport_test_adapter PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_include_directories(management_transport_test_adapter PUBLIC
    support
    support/management
    "${REPOSITORY_ROOT}/Management/include"
    "${REPOSITORY_ROOT}/Configuration/include"
    "${REPOSITORY_ROOT}/MQTT/include"
    "${LWIP_ROOT}/src/include"
    "${LWIP_ROOT}/system"
)

target_include_directories(management_transport_test_adapter PRIVATE
    "${REPOSITORY_ROOT}/SNTP/include"
)

add_executable(management_transport_tests
    Management/test_management_transport.c
)

target_compile_definitions(management_transport_tests PRIVATE
    MANAGEMENT_TRANSPORT_TEST
)

target_compile_options(management_transport_tests PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_include_directories(management_transport_tests PRIVATE
    support/management
)

target_link_libraries(management_transport_tests PRIVATE
    unity
    management_transport_runtime
    management_transport_test_adapter
)

add_test(NAME management_transport_tests COMMAND management_transport_tests)
set_tests_properties(management_transport_tests PROPERTIES LABELS "management;transport")

add_library(configuration_runtime STATIC
    "${REPOSITORY_ROOT}/Configuration/src/configuration.c"
    "${REPOSITORY_ROOT}/Configuration/src/configuration_binary_codec.c"
    "${LWIP_ROOT}/src/core/def.c"
    "${LWIP_ROOT}/src/core/ipv4/ip4_addr.c"
    "${MBEDTLS_ROOT}/library/asn1parse.c"
    "${MBEDTLS_ROOT}/library/asn1write.c"
    "${MBEDTLS_ROOT}/library/base64.c"
    "${MBEDTLS_ROOT}/library/bignum.c"
    "${MBEDTLS_ROOT}/library/ecdsa.c"
    "${MBEDTLS_ROOT}/library/ecp.c"
    "${MBEDTLS_ROOT}/library/ecp_curves.c"
    "${MBEDTLS_ROOT}/library/md.c"
    "${MBEDTLS_ROOT}/library/md_wrap.c"
    "${MBEDTLS_ROOT}/library/oid.c"
    "${MBEDTLS_ROOT}/library/pem.c"
    "${MBEDTLS_ROOT}/library/pk.c"
    "${MBEDTLS_ROOT}/library/pk_wrap.c"
    "${MBEDTLS_ROOT}/library/pkparse.c"
    "${MBEDTLS_ROOT}/library/platform.c"
    "${MBEDTLS_ROOT}/library/platform_util.c"
    "${MBEDTLS_ROOT}/library/rsa.c"
    "${MBEDTLS_ROOT}/library/rsa_internal.c"
    "${MBEDTLS_ROOT}/library/sha256.c"
    "${MBEDTLS_ROOT}/library/sha512.c"
    "${MBEDTLS_ROOT}/library/x509.c"
    "${MBEDTLS_ROOT}/library/x509_crt.c"
    support/mbedtls_platform_host.c
)

target_compile_definitions(configuration_runtime PRIVATE
    MBEDTLS_CONFIG_FILE="mbedtls_config.h"
)

target_compile_options(configuration_runtime PRIVATE
    -Wall
    -Wextra
    -ffunction-sections
    -fdata-sections
)

set_source_files_properties(support/mbedtls_platform_host.c PROPERTIES
    COMPILE_OPTIONS -Werror
)

target_include_directories(configuration_runtime PUBLIC
    "${REPOSITORY_ROOT}/Configuration/include"
    "${LWIP_ROOT}/src/include"
    "${LWIP_ROOT}/system"
    "${MBEDTLS_ROOT}/include"
)

target_include_directories(configuration_runtime PRIVATE
    support
    "${REPOSITORY_ROOT}/MBEDTLS/App"
)

add_library(configuration_test_support STATIC
    support/configuration_test_support.c
)

target_compile_definitions(configuration_test_support PRIVATE
    CONFIGURATION_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Configuration/fixtures"
    MBEDTLS_CONFIG_FILE="mbedtls_config.h"
)

target_compile_options(configuration_test_support PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_include_directories(configuration_test_support PUBLIC
    support
)

target_include_directories(configuration_test_support PRIVATE
    "${REPOSITORY_ROOT}/MBEDTLS/App"
)

target_link_libraries(configuration_test_support PUBLIC
    configuration_runtime
)

add_library(configuration_service_runtime STATIC
    "${REPOSITORY_ROOT}/Configuration/src/configuration_binary_codec.c"
    "${REPOSITORY_ROOT}/Configuration/src/configuration_service.c"
    "${REPOSITORY_ROOT}/Middlewares/Third_Party/RTT/SEGGER_RTT.c"
    "${REPOSITORY_ROOT}/Middlewares/Third_Party/RTT/SEGGER_RTT_printf.c"
)

target_compile_definitions(configuration_service_runtime PRIVATE
    CONFIGURATION_SERVICE_TEST
    LWIP_ALTCP=1
    LWIP_ALTCP_TLS=1
    LWIP_ALTCP_TLS_MBEDTLS=1
    MBEDTLS_CONFIG_FILE="mbedtls_config.h"
)

target_compile_options(configuration_service_runtime PRIVATE
    -Wall
    -Wextra
    -Werror
    -ffunction-sections
    -fdata-sections
)

target_include_directories(configuration_service_runtime PRIVATE
    support
    "${REPOSITORY_ROOT}/Core/Inc"
    "${REPOSITORY_ROOT}/Configuration/include"
    "${REPOSITORY_ROOT}/ExternalFlash/include"
    "${REPOSITORY_ROOT}/MBEDTLS/App"
    "${REPOSITORY_ROOT}/Middlewares/Third_Party/RTT"
    "${LWIP_ROOT}/src/include"
    "${LWIP_ROOT}/src/apps/altcp_tls"
    "${LWIP_ROOT}/system"
    "${MBEDTLS_ROOT}/include"
)

target_link_libraries(configuration_service_runtime PUBLIC
    configuration_runtime
)

add_library(configuration_service_test_adapter STATIC
    support/configuration_service_test_adapter.c
)

target_compile_options(configuration_service_test_adapter PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_include_directories(configuration_service_test_adapter PRIVATE
    support
    "${REPOSITORY_ROOT}/ExternalFlash/include"
    "${LWIP_ROOT}/src/include"
    "${LWIP_ROOT}/system"
)

function(add_configuration_test target source)
    add_executable(${target} ${source})
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
    target_link_libraries(${target} PRIVATE unity configuration_test_support)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS configuration)
endfunction()

add_configuration_test(configuration_defaults_tests
    Configuration/test_configuration_defaults.c
)

add_configuration_test(configuration_validate_tests
    Configuration/test_configuration_validate.c
)

add_configuration_test(configuration_equals_tests
    Configuration/test_configuration_equals.c
)

add_configuration_test(configuration_property_tests
    Configuration/test_configuration_properties.c
)

add_configuration_test(configuration_binary_codec_tests
    Configuration/test_configuration_binary_codec.c
)

add_executable(configuration_service_tests
    Configuration/test_configuration_service.c
)

target_compile_options(configuration_service_tests PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_link_options(configuration_service_tests PRIVATE
    -Wl,--gc-sections
)

target_link_libraries(configuration_service_tests PRIVATE
    unity
    configuration_test_support
    configuration_service_runtime
    configuration_service_test_adapter
)

add_test(NAME configuration_service_tests COMMAND configuration_service_tests)
set_tests_properties(configuration_service_tests PROPERTIES LABELS "configuration;service")

add_executable(mqtt_publisher_client_id_tests
    MQTT/test_mqtt_publisher_client_id.c
    "${REPOSITORY_ROOT}/MQTT/src/mqtt_publisher.c"
)

target_compile_definitions(mqtt_publisher_client_id_tests PRIVATE
    MQTT_PUBLISHER_CLIENT_ID_TEST
)

target_compile_options(mqtt_publisher_client_id_tests PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_include_directories(mqtt_publisher_client_id_tests PRIVATE
    "${REPOSITORY_ROOT}/MQTT/include"
)

target_link_libraries(mqtt_publisher_client_id_tests PRIVATE
    unity
)

add_test(NAME mqtt_publisher_client_id_tests COMMAND mqtt_publisher_client_id_tests)
set_tests_properties(mqtt_publisher_client_id_tests PROPERTIES LABELS "mqtt;client-id")

add_executable(modbus_collector_codec_tests
    Modbus/test_modbus_collector_codec.c
    support/modbus_rtu_rs485_port_test_adapter.c
    "${REPOSITORY_ROOT}/Modbus/src/modbus_collector_codec.c"
    "${REPOSITORY_ROOT}/Modbus/src/modbus_rtu.c"
)

target_compile_options(modbus_collector_codec_tests PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_include_directories(modbus_collector_codec_tests PRIVATE
    support
    "${REPOSITORY_ROOT}/Configuration/include"
    "${REPOSITORY_ROOT}/Modbus/include"
    "${LWIP_ROOT}/src/include"
    "${LWIP_ROOT}/system"
)

target_link_libraries(modbus_collector_codec_tests PRIVATE
    unity
)

add_test(NAME modbus_collector_codec_tests COMMAND modbus_collector_codec_tests)
set_tests_properties(modbus_collector_codec_tests PROPERTIES LABELS "modbus;collector;codec")

set_tests_properties(configuration_property_tests PROPERTIES
    LABELS "configuration;properties"
    TIMEOUT 120
)
