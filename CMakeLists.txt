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

# HMS benchmark package
set(benchmark_path ${CMAKE_CURRENT_SOURCE_DIR}/packages/benchmark_hms)
include(${benchmark_path}/CMakeLists.txt)

target_link_libraries(app PRIVATE bmth)
