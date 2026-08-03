# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blinky)

target_sources(app PRIVATE timing/time_task_switch.c)

# HMS benchmark package: common code plus the Zephyr port for this board.
set(benchmark_path ${CMAKE_CURRENT_SOURCE_DIR}/packages/benchmark_hms)
set(benchmark_port_path ${benchmark_path}/port)

target_include_directories(app PRIVATE
  ${benchmark_path}
  ${benchmark_port_path}/frdm_mcxn947/zephyr
)

target_sources(app PRIVATE
  ${benchmark_path}/benchmark_tools.c
  ${benchmark_port_path}/frdm_mcxn947/zephyr/benchmark_zephyr.c
  ${benchmark_port_path}/core/benchmark_m33.c
)
