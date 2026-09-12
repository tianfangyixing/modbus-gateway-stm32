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
