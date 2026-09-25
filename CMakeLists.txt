# SPDX-License-Identifier: Apache-2.0

# The SDK drives the C standard through raw -std flags and leaves
# CMAKE_C_STANDARD unset.
set(CMAKE_C_STANDARD 11)

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blinky)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                      ${CMAKE_CURRENT_SOURCE_DIR}/src/task_switching
                                      ${CMAKE_CURRENT_SOURCE_DIR}/src/zephyr_api_simulation)

target_sources(app PRIVATE src/bmth_port.c)

# Zephyr API simulation: the kernel chains that cannot be timed in the kernel
# itself, re-run without a context switch.
target_sources(app PRIVATE src/zephyr_api_simulation/sim_init.c
                           src/zephyr_api_simulation/sim_sched.c)

# Exactly one benchmark test is linked in; the choice in Kconfig picks it.
# Each test directory carries its own zephyr_sim.c with the measure_*_overhead()
# probes it needs, built on the shared simulation in zephyr_api_simulation/.
target_sources_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCHING_EVENT_PREEMPT app PRIVATE
                     src/task_switching/event_preempt/time_task_switch_event.c
                     src/task_switching/event_preempt/zephyr_sim.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCHING_SEM_PREEMPT app PRIVATE
                     src/task_switching/sem_preempt/time_task_switch_sem.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCHING_EVENT_BLOCK app PRIVATE
                     src/task_switching/event_block/time_task_switch_event.c
                     src/task_switching/event_block/zephyr_sim.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCHING_TASK_CHANGE app PRIVATE
                     src/task_switching/task_change/time_task_switch.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCHING_TASK_PREEMPT app PRIVATE
                     src/task_switching/task_preempt/time_task_switch.c
                     src/task_switching/task_preempt/zephyr_sim.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_IRT app PRIVATE
                     src/irt/time_interrupt_response.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_ITC_MAILBOX_ASYNC app PRIVATE
                     src/itc/mailbox/mailbox_async.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_ITC_FIFO app PRIVATE
                     src/itc/fifo/fifo.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_ITC_LIFO app PRIVATE
                     src/itc/lifo/lifo.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_ITC_STACK app PRIVATE
                     src/itc/stack/stack.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_ITC_MSGQ app PRIVATE
                     src/itc/msgq/msgq.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_ITC_PIPE_ASYNC app PRIVATE
                     src/itc/pipe/pipe_async.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_ITC_MAILBOX_SYNC app PRIVATE
                     src/itc/mailbox/mailbox_sync.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_ITC_PIPE_SYNC app PRIVATE
                     src/itc/pipe/pipe_sync.c)

target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCHING_TASK_PREEMPT app PRIVATE
                                src/task_switching/task_preempt)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCHING_TASK_CHANGE app PRIVATE
                                src/task_switching/task_change)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCHING_EVENT_PREEMPT app PRIVATE
                           src/task_switching/event_preempt)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCHING_EVENT_BLOCK app PRIVATE
                           src/task_switching/event_block)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCHING_SEM_PREEMPT app PRIVATE
                           src/task_switching/sem_preempt)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_IRT app PRIVATE
                           src/irt)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_ITC_MAILBOX_ASYNC app PRIVATE
                           src/itc/mailbox)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_ITC_FIFO app PRIVATE
                           src/itc/fifo)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_ITC_LIFO app PRIVATE
                           src/itc/lifo)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_ITC_STACK app PRIVATE
                           src/itc/stack)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_ITC_MSGQ app PRIVATE
                           src/itc/msgq)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_ITC_PIPE_ASYNC app PRIVATE
                           src/itc/pipe)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_ITC_MAILBOX_SYNC app PRIVATE
                           src/itc/mailbox)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_ITC_PIPE_SYNC app PRIVATE
                           src/itc/pipe)

# The allocation tests. One adapter per allocator, plus the portable workload
# in mem_harness.c and the one Zephyr-aware file they share. The adapter is the
# only file that knows which allocator is under test.
target_sources_ifdef(CONFIG_BENCHMARK_TEST_MEM_SYS_HEAP app PRIVATE
                     src/mem/sys_heap/sys_heap.c
                     src/mem/mem_harness.c
                     src/mem/mem_zephyr_main.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_MEM_K_HEAP app PRIVATE
                     src/mem/k_heap/k_heap.c
                     src/mem/mem_harness.c
                     src/mem/mem_zephyr_main.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_MEM_K_MALLOC app PRIVATE
                     src/mem/k_malloc/k_malloc.c
                     src/mem/mem_harness.c
                     src/mem/mem_zephyr_main.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_MEM_SLAB app PRIVATE
                     src/mem/mem_slab/mem_slab.c
                     src/mem/mem_harness.c
                     src/mem/mem_zephyr_main.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_MEM_BLOCKS app PRIVATE
                     src/mem/mem_blocks/mem_blocks.c
                     src/mem/mem_harness.c
                     src/mem/mem_zephyr_main.c)

# mem_allocator.h and mem_harness.h are shared by all five: one workload, one
# measurement window, so the numbers compare.
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_SYS_HEAP app PRIVATE
                                src/mem)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_K_HEAP app PRIVATE
                                src/mem)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_K_MALLOC app PRIVATE
                                src/mem)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_SLAB app PRIVATE
                                src/mem)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_BLOCKS app PRIVATE
                                src/mem)

# src/mem/mem_zephyr_heap.h reads the heap's own chunk walk to report the
# total used space and the fixed arena cost, which needs lib/heap/heap.h. That
# header is private to the kernel and is not on the default include path. Only
# the three heap cases include it; the slab and the blocks cases read their own
# allocator's state and must not gain the path.
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_SYS_HEAP app PRIVATE
                                ${ZEPHYR_BASE}/lib/heap)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_K_HEAP app PRIVATE
                                ${ZEPHYR_BASE}/lib/heap)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_K_MALLOC app PRIVATE
                                ${ZEPHYR_BASE}/lib/heap)

# HMS benchmark package
set(benchmark_path ${CMAKE_CURRENT_SOURCE_DIR}/packages/benchmark_hms)
include(${benchmark_path}/CMakeLists.txt)

target_link_libraries(app PRIVATE bmth)
