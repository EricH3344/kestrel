# Host-side unit tests for the CRSF protocol module (plan Part 3).
#   cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(kestrel_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(FW_CORE ${CMAKE_CURRENT_SOURCE_DIR}/../Kestrel_Gimbal_Test/Core)

add_executable(test_crsf
    test_crsf.c
    ${FW_CORE}/Src/crsf.c
)
target_include_directories(test_crsf PRIVATE ${FW_CORE}/Inc)
target_compile_options(test_crsf PRIVATE -Wall -Wextra -O2)

enable_testing()
add_test(NAME crsf COMMAND test_crsf)
