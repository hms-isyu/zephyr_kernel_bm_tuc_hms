# Task switching

The `task_switching` family measures what a switch from one task to another costs when a
Zephyr operation triggers it. The members differ in the operation that triggers the switch.

## Terminology

| Term | Meaning |
| --- | --- |
| `S_A` | The scenario in which the measured operation ends in a switch. |
| `S_B` | The scenario in which the same operation leaves the calling task on the CPU. |
| `S_A - S_B` | The difference of the output values of the two scenarios of one test case. |
| iteration cycle | One pass from a start marker to its stop marker, and the steps that prepare the next pass. |
| control task | The task that creates the tasks of each scenario, drives the iteration cycles and removes the tasks. |
| dummy task, `D` | A task created only as load. It takes no marker. `D` stands for any one dummy task of a scenario. |

## Scenarios

| Scenario | Trigger | Measured path |
| --- | --- | --- |
| `S_A` | The calling task runs the measured operation under conditions that hand the CPU to another task. | The operation up to the switch, the switch, and the tail of the call in which the receiving task last gave up the CPU. Start marker in the calling task, stop marker in the receiving task. |
| `S_B` | The calling task runs the same operation under conditions in which it keeps the CPU. | The operation up to its return. Both markers in the calling task. |

## Measurement principle

1. The output value is the cycle count between the start marker and the stop marker.
2. Each test case measures one operation on two paths: `S_A`, which ends in a switch, and
   `S_B`, on which the calling task keeps the CPU.
3. `S_A - S_B` holds only what the two paths do not share.
4. The control task is outranked by every other task of a scenario. It runs only while every
   other task is blocked or suspended, so none of its statements falls inside a measurement
   window.
5. A loaded measurement differs from its base measurement only in the dummy tasks it creates.
   Which queue they wait in, and which scenario they act on, is a property of the test case.
6. The code that creates and removes the dummy tasks is the same at every point of the sweep,
   so the sweep adds no code path of its own.
7. The load size is swept one build per value, by hand: no script changes
   `DUMMY_TASKS_COUNT`. A value committed in the source is a position in the sweep,
   not a property of the test case.
8. A part of a measured call that cannot be timed in the kernel, because a marker would have to
   sit inside the switch, is re-run without the switch and recorded as its own output value.
   Nothing is subtracted from a recorded value.
9. `S_A` runs first, then `S_B`. A task used by one scenario only is removed before the tasks
   of the other scenario are created.

## Configuration

A directory is built by setting its `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_*` symbol to y in
`prj.conf`. The symbols belong to the Kconfig choice `BENCHMARK_TEST`, so one build holds one
directory. Inside it, `#define TEST_CASE` in `benchmark_testcases.h` selects the one test case
that is built. Shared by every test case:

| Setting | Value | Set in | Meaning |
| --- | --- | --- | --- |
| `MEASUREMENT_COUNT` | 50000 | test source | iteration cycles per scenario |
| `ADD_DUMMY_TASKS` | 1 in the branch of a loaded measurement | `benchmark_testcases.h` | the test case creates dummy tasks |
| `DUMMY_TASKS_COUNT` | the load size, domain `0..DUMMY_POOL_SIZE`, `DUMMY_POOL_SIZE` = 4 | `benchmark_testcases.h`, `DUMMY_POOL_SIZE` in the test source | `dummy_count = min(DUMMY_TASKS_COUNT, DUMMY_POOL_SIZE)` dummy tasks per scenario with `ADD_DUMMY_TASKS` = 1, else `dummy_count` = 0. `dummy_count` is read at run time wherever it bounds a loop. |

## Family members

| Directory | What separates it from its neighbours |
| --- | --- |
| [`task_change`](task_change/README.md) | `k_yield()` re-inserts the yielding task at its own priority. |
| [`task_preempt`](task_preempt/README.md) | `k_thread_resume()` acts on a task that waits in no wait queue. |
| [`sem_preempt`](sem_preempt/README.md) | `k_sem_give()` reaches at most one task of its wait queue. |
| [`event_preempt`](event_preempt/README.md) | `k_event_post()` reaches every matching task of its wait queue. |
| [`event_block`](event_block/README.md) | `k_event_wait_safe()` is the call in which the measured task itself blocks. |
