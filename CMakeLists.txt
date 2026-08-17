# SPDX-License-Identifier: Apache-2.0

# The SDK drives the C standard through raw -std flags and leaves
# CMAKE_C_STANDARD unset.
set(CMAKE_C_STANDARD 11)

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blinky)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/timing)

target_sources(app PRIVATE timing/time_task_switch.c timing/time_epilogue_simulate.c
                           timing/bmth_port.c)

# HMS benchmark package
set(benchmark_path ${CMAKE_CURRENT_SOURCE_DIR}/packages/benchmark_hms)
include(${benchmark_path}/CMakeLists.txt)

target_link_libraries(app PRIVATE bmth)
