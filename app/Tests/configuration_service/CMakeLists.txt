cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(configuration_service_host_tests C)
    enable_testing()
endif()

if(NOT TARGET configuration_model)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../common/CMakeLists.txt")
        message(FATAL_ERROR "Import the Configuration v2 core host-test dependency (Tests/common) first")
    endif()
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../common" "${CMAKE_CURRENT_BINARY_DIR}/common")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(storage_fixture_dir "${CMAKE_CURRENT_LIST_DIR}/../common/fixtures")
set(storage_record_header "${CMAKE_CURRENT_BINARY_DIR}/generated/storage_record_fixtures.h")
add_custom_command(
    OUTPUT "${storage_record_header}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate_records.py"
            --fixtures "${storage_fixture_dir}" --output "${storage_record_header}"
    DEPENDS generate_records.py "${storage_fixture_dir}/default_v2.bin" "${storage_fixture_dir}/maximum_v2.bin"
    VERBATIM
)

add_executable(configuration_service_tests
    test_configuration_service.c
    flash_simulator.c
    ../../Configuration/src/configuration_service.c
    "${storage_record_header}"
)
target_compile_features(configuration_service_tests PRIVATE c_std_11)
target_compile_definitions(configuration_service_tests PRIVATE CONFIGURATION_SERVICE_TEST)
target_include_directories(configuration_service_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/adapters"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_LIST_DIR}/../../ExternalFlash/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../Core/Inc"
)
target_link_libraries(configuration_service_tests PRIVATE configuration_model unity)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(configuration_service_tests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME configuration_service_tests COMMAND configuration_service_tests)
