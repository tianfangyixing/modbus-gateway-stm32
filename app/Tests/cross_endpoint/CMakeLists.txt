add_executable(cross_endpoint_vectors review_vectors.c)
target_link_libraries(cross_endpoint_vectors PRIVATE configuration_model management_frame_host)
target_compile_features(cross_endpoint_vectors PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cross_endpoint_vectors PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME cross_endpoint_vectors
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_c.py"
        --executable "$<TARGET_FILE:cross_endpoint_vectors>"
        --vectors-dir "${CMAKE_CURRENT_SOURCE_DIR}/vectors"
        --output "${CMAKE_CURRENT_BINARY_DIR}/c_results.json")
