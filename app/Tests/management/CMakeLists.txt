cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(management_host_tests C)
    enable_testing()
endif()

get_filename_component(MANAGEMENT_APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_library(management_frame_host STATIC "${MANAGEMENT_APP_DIR}/Management/src/management_frame.c")
target_include_directories(management_frame_host PUBLIC "${MANAGEMENT_APP_DIR}/Management/include")
target_compile_features(management_frame_host PUBLIC c_std_11)

add_executable(management_frame_tests test_management_frame.c)
target_link_libraries(management_frame_tests PRIVATE management_frame_host)
add_test(NAME management_frame COMMAND management_frame_tests)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME management_python COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/test_management_tools.py")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(management_frame_host PRIVATE -Wall -Wextra -Werror)
    target_compile_options(management_frame_tests PRIVATE -Wall -Wextra -Werror)
endif()

option(MANAGEMENT_TEST_CONFIGURATION_PATH "Test Transport with production Configuration and Service" ON)
if(MANAGEMENT_TEST_CONFIGURATION_PATH)
    if(NOT TARGET configuration_model)
        add_subdirectory("../common" "${CMAKE_CURRENT_BINARY_DIR}/common")
    endif()
    add_executable(management_transport_tests
        "${MANAGEMENT_APP_DIR}/Management/src/management_transport.c"
        "${MANAGEMENT_APP_DIR}/Configuration/src/configuration_service.c"
        "../configuration_service/flash_simulator.c"
        management_transport_test_adapter.c
        test_management_transport.c)
    target_include_directories(management_transport_tests BEFORE PRIVATE
        adapter "../configuration_service"
        "${MANAGEMENT_APP_DIR}/Configuration/include"
        "${MANAGEMENT_APP_DIR}/ExternalFlash/include"
        "${MANAGEMENT_APP_DIR}/MQTT/include"
        "${MANAGEMENT_APP_DIR}/SNTP/include"
        "${MANAGEMENT_APP_DIR}/Boot"
        "${MANAGEMENT_APP_DIR}/Core/Inc")
    target_compile_definitions(management_transport_tests PRIVATE
        MANAGEMENT_TRANSPORT_TEST CONFIGURATION_SERVICE_TEST)
    target_link_libraries(management_transport_tests PRIVATE
        management_frame_host configuration_test_fixtures unity)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(management_transport_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
    endif()
    add_test(NAME management_transport COMMAND management_transport_tests)
endif()
