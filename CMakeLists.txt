# SPDX-License-Identifier: Apache-2.0

# The SDK drives the C standard through raw -std flags and leaves
# CMAKE_C_STANDARD unset.
set(CMAKE_C_STANDARD 11)

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blinky)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/timing
                                      ${CMAKE_CURRENT_SOURCE_DIR}/timing/task_switch
                                      ${CMAKE_CURRENT_SOURCE_DIR}/timing/zephyr_api_simulation)

target_sources(app PRIVATE timing/bmth_port.c)

# Zephyr API simulation: the kernel chains that cannot be timed in the kernel
# itself, re-run without a context switch.
target_sources(app PRIVATE timing/zephyr_api_simulation/sim_init.c
                           timing/zephyr_api_simulation/sim_sched.c)

# Exactly one benchmark test is linked in; the choice in Kconfig picks it.
# Each test directory carries its own zephyr_sim.c with the measure_*_overhead()
# probes it needs, built on the shared simulation in zephyr_api_simulation/.
target_sources_ifdef(CONFIG_BENCHMARK_TEST_EVENT_TASK_SWITCH_PREEMPT app PRIVATE
                     timing/task_switch/event_preempt/time_task_switch_event.c
                     timing/task_switch/event_preempt/zephyr_sim.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_SEM_TASK_SWITCH_PREEMPT app PRIVATE
                     timing/task_switch/sem_preempt/time_task_switch_sem.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_EVENT_TASK_SWITCH_BLOCK app PRIVATE
                     timing/task_switch/event_block/time_task_switch_event.c
                     timing/task_switch/event_block/zephyr_sim.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCH_CHANGE app PRIVATE
                     timing/task_switch/task_change/time_task_switch.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCH_PREEMPT app PRIVATE
                     timing/task_switch/task_preempt/time_task_switch.c
                     timing/task_switch/task_preempt/zephyr_sim.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_INTERRUPT_RESPONSE_TIME app PRIVATE
                     timing/interrupt_latency/time_interrupt_response.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_IPC_MAILBOX_ASYNC app PRIVATE
                     timing/ipc/mailbox/mailbox_async.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_IPC_FIFO app PRIVATE
                     timing/ipc/fifo/fifo.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_IPC_LIFO app PRIVATE
                     timing/ipc/lifo/lifo.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_IPC_STACK app PRIVATE
                     timing/ipc/stack/stack.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_IPC_MSGQ app PRIVATE
                     timing/ipc/msgq/msgq.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_IPC_PIPE_ASYNC app PRIVATE
                     timing/ipc/pipe/pipe_async.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_IPC_MAILBOX_SYNC app PRIVATE
                     timing/ipc/mailbox/mailbox_sync.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_IPC_PIPE_SYNC app PRIVATE
                     timing/ipc/pipe/pipe_sync.c)

target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCH_PREEMPT app PRIVATE
                                timing/task_switch/task_preempt)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_TASK_SWITCH_CHANGE app PRIVATE
                                timing/task_switch/task_change)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_EVENT_TASK_SWITCH_PREEMPT app PRIVATE
                           timing/task_switch/event_preempt)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_EVENT_TASK_SWITCH_BLOCK app PRIVATE
                           timing/task_switch/event_block)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_SEM_TASK_SWITCH_PREEMPT app PRIVATE
                           timing/task_switch/sem_preempt)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_INTERRUPT_RESPONSE_TIME app PRIVATE
                           timing/interrupt_latency)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_IPC_MAILBOX_ASYNC app PRIVATE
                           timing/ipc/mailbox)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_IPC_FIFO app PRIVATE
                           timing/ipc/fifo)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_IPC_LIFO app PRIVATE
                           timing/ipc/lifo)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_IPC_STACK app PRIVATE
                           timing/ipc/stack)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_IPC_MSGQ app PRIVATE
                           timing/ipc/msgq)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_IPC_PIPE_ASYNC app PRIVATE
                           timing/ipc/pipe)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_IPC_MAILBOX_SYNC app PRIVATE
                           timing/ipc/mailbox)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_IPC_PIPE_SYNC app PRIVATE
                           timing/ipc/pipe)

# The allocation tests. One adapter per allocator, plus the portable workload
# in mem_harness.c and the one Zephyr-aware file they share. The adapter is the
# only file that knows which allocator is under test.
target_sources_ifdef(CONFIG_BENCHMARK_TEST_MEM_SYS_HEAP app PRIVATE
                     timing/mem/sys_heap/sys_heap.c
                     timing/mem/mem_harness.c
                     timing/mem/mem_zephyr_main.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_MEM_K_HEAP app PRIVATE
                     timing/mem/k_heap/k_heap.c
                     timing/mem/mem_harness.c
                     timing/mem/mem_zephyr_main.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_MEM_K_MALLOC app PRIVATE
                     timing/mem/k_malloc/k_malloc.c
                     timing/mem/mem_harness.c
                     timing/mem/mem_zephyr_main.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_MEM_SLAB app PRIVATE
                     timing/mem/mem_slab/mem_slab.c
                     timing/mem/mem_harness.c
                     timing/mem/mem_zephyr_main.c)

target_sources_ifdef(CONFIG_BENCHMARK_TEST_MEM_BLOCKS app PRIVATE
                     timing/mem/mem_blocks/mem_blocks.c
                     timing/mem/mem_harness.c
                     timing/mem/mem_zephyr_main.c)

# mem_allocator.h and mem_harness.h are shared by all five: one workload, one
# measurement window, so the numbers compare.
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_SYS_HEAP app PRIVATE
                                timing/mem)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_K_HEAP app PRIVATE
                                timing/mem)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_K_MALLOC app PRIVATE
                                timing/mem)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_SLAB app PRIVATE
                                timing/mem)
target_include_directories_ifdef(CONFIG_BENCHMARK_TEST_MEM_BLOCKS app PRIVATE
                                timing/mem)

# timing/mem/mem_zephyr_heap.h reads the heap's own chunk walk to report the
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
