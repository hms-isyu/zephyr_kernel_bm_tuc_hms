# Task switching

The `task_switching` family measures what a switch from one task to another costs when a
Zephyr operation triggers it. Each test case runs one operation twice: once where it hands the
CPU to another task, once where the calling task keeps the CPU. The members differ in the
operation and in the queue that operation works on.

## Terminology

| Name | Meaning |
| --- | --- |
| `S_A` | The scenario in which the measured operation ends in a switch. Recorded into the series `scenario_a`. |
| `S_B` | The scenario in which the same operation leaves the calling task on the CPU. Recorded into the series `scenario_b`. |
| `X > Y`, `X == Y` | Task `X` outranks task `Y`: `X` has the lower priority number. `X == Y`: the same priority. |
| `D` | Any one dummy task: an entry of `sa_dummy_threads[]` in `S_A`, of `sb_dummy_threads[]` in `S_B`. A dummy task takes no marker. |
| `I_Task` | The control task. Creates the tasks of each scenario, drives the rounds, removes the tasks, reports. |
| `TEST_CASE` | The `#define` in the `benchmark_testcases.h` of a directory that selects one of its test cases. |
| `DUMMY_TASKS_COUNT` | The number of dummy tasks per scenario, the sweep parameter, domain `0..DUMMY_POOL_SIZE`. |
| base measurement, loaded measurement | A test case that creates no dummy task, and one that creates `DUMMY_TASKS_COUNT` of them. |
| probe | A function in the `zephyr_sim.c` of a directory that re-runs one part of a measured call without switching and records it as its own series. |
| prologue, tail | The part of a call before, and the part after, one interrupt unlock: on the switching path the `irq_unlock()` in `arch_swap()` at which the PendSV exception is taken, on the non-switching path the unlock that ends the locked section of the call. |

## Scenarios

| Scenario | Series | Trigger | Measured path |
| --- | --- | --- | --- |
| `S_A` | `scenario_a` | The task the test case names executes the operation under conditions that hand the CPU to another task. | The operation up to the switch, the switch, and the tail of the call the receiving task was waiting in. Start marker in the calling task, stop marker in the receiving task. |
| `S_B` | `scenario_b` | The same operation under conditions in which the calling task keeps the CPU. | The operation up to its return. Both markers in the calling task. |

Up to the point where the two paths part, `S_A` and `S_B` run the same code, and that code
cancels in `S_A - S_B`. What remains is the `S_A` path from that point to its stop marker, less
the `S_B` path from that point to its stop marker.

## Measurement principle

1. The output value is the cycle count between the start marker and the stop marker.
2. Each test case measures one operation on two paths: one that ends in a switch, one on which
   the calling task keeps the CPU.
3. The difference of the two holds only what the two paths do not share.
4. A loaded test case differs from its base test case only in the dummy tasks it creates.
   Which queue they wait in, and which scenario they act on, is a property of the test case.
5. The load size is read at run time wherever it bounds a loop, so the code that creates and
   removes the dummy tasks is the same at every point of the sweep.
6. The load size is swept one build per value. A value committed in the source is a position in
   the sweep, not a property of the test case.
7. A part of a measured call that cannot be timed in the kernel, because a marker would have to
   sit inside the context switch, is re-run by a probe without the switch and recorded as its
   own series. Nothing is subtracted from a recorded value.
8. The scenarios run one after the other. A task used by one scenario only is removed before
   the tasks of the other scenario are created.

## Configuration

`prj.conf` of the repository, and the kernel options it leaves to their Kconfig defaults.

| Option | Value | Consequence for the family |
| --- | --- | --- |
| `CONFIG_MP_MAX_NUM_CPUS` | 1 | one ready queue, one task runs at a time |
| `CONFIG_WAITQ_SIMPLE` | y, Kconfig default, not set in `prj.conf` | every wait queue is one priority sorted list. An insert (`z_priq_simple_add()`) walks from the head past every task the inserted one does not outrank and stops at the first task it outranks, so a task queues behind every task `==` or `>` itself. Taking the head (`z_priq_simple_best()`) is `O(1)`. |
| `CONFIG_SCHED_MULTIQ` | n in `prj.conf`, so `CONFIG_SCHED_SIMPLE` resolves to y. y is a separate build. | n: the ready queue is one priority sorted list with the insert rule of the `CONFIG_WAITQ_SIMPLE` row, and the `k_yield()` re-insert (`z_priq_simple_yield()`) walks on from the position of the yielding task. y: one list per priority plus a bitmask (`z_priq_mq_*`), insert, remove, yield and pick are `O(1)`. |
| `CONFIG_USE_SWITCH` | n, resolved, not set in `prj.conf` | `z_swap()` calls `arch_swap()`, which pends PendSV without comparing the next task to the current one. The handler `z_arm_pendsv` saves the outgoing task and restores `_kernel.ready_q.cache`, also when that is the same task. A call leaves its caller on the CPU without the PendSV exception only where it tests `need_swap()` before `z_swap()` (`reschedule()`), or where it never calls `z_swap()`. |
| `CONFIG_TIMESLICING` | n | tasks of equal priority change only on an explicit call |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | priorities 0..15 are preemptible. The kernel idle thread sits at `K_IDLE_PRIO` = 16. `I_Task` runs at `TASK_IDLE_PRIO` = 15, and every other test task `> I_Task`. |
| `CONFIG_EVENTS` | y | `k_event` exists. Every member signals its start through the `k_event` object `test_event`. |
| `CONFIG_TICKLESS_KERNEL`, `CONFIG_SYS_CLOCK_TICKS_PER_SEC` | y, 100 | no periodic tick |
| `CONFIG_FORCE_NO_ASSERT` | y | the `__ASSERT` statements inside the measured operations expand to nothing |
| `CONFIG_DEBUG_OPTIMIZATIONS` | y | `-Og` for every test case |

Selection. One test case is linked per build, through the Kconfig choice `BENCHMARK_TEST`: set
one symbol with the prefix `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_` to y in `prj.conf`. The
symbol selects the directory. Inside it, `#define TEST_CASE` in `benchmark_testcases.h` selects
the test case. Each `TEST_CASE` branch defines the priorities and `ADD_DUMMY_TASKS`, and the
branch of a loaded test case defines `DUMMY_TASKS_COUNT`, which otherwise defaults to 0 at the
end of that file. With `ADD_DUMMY_TASKS` = 1 the test source sets
`dummy_count = min(DUMMY_TASKS_COUNT, DUMMY_POOL_SIZE)`, else `dummy_count` = 0.
`DUMMY_POOL_SIZE` = 4 is defined in the test source.

Bookkeeping, identical in every test case. `MEASUREMENT_COUNT` = 50000 rounds per scenario.
The dummy pools `sa_dummy_threads[]` and `sb_dummy_threads[]` are allocated at
`DUMMY_POOL_SIZE` in every build, and `dummy_count` is `volatile`, so the create and abort
loops it bounds compile to the same code at every point of the sweep. `BMTH_mwindow_open()`
and `BMTH_mwindow_close()` gate `BMTH_mseries_iterate()`: an iteration is recorded only if its
window is open at the stop marker, so a task that calls `BMTH_mwindow_close()` inside an
open window discards that iteration. `scenario_a` is initialised as
`BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS`, the mode for markers in two
different tasks, `scenario_b` as `BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS`,
the mode for markers in one function. Before `I_Task` is created, `main()` turns the low power
cache controller off (`SYSCON->LPCAC_CTRL`), disables the SysTick, validates the DWT cycle
counter with `BMTH_check_read_validity()` and, in a directory with a `zephyr_sim.c`, runs each
probe for `MEASUREMENT_COUNT` iterations. `I_Task` reports through
`BMTH_signalize_mseries_stop()`, with `false` if `scenario_a` or `scenario_b` counted an
outlier.

## Family members

| Directory | Readme | What separates it |
| --- | --- | --- |
| `task_change` | [task_change/README.md](task_change/README.md) | `k_yield()` between tasks of equal priority. `k_yield()` calls `z_swap()` in both scenarios. The dummy tasks are ready tasks `==` the measured pair. |
| `task_preempt` | [task_preempt/README.md](task_preempt/README.md) | `k_thread_resume()` on a suspended task. No wait queue in the window. The dummy tasks are ready tasks in `S_B` only. |
| `sem_preempt` | [sem_preempt/README.md](sem_preempt/README.md) | `k_sem_give()`, which takes the head of the wait queue only. The dummy tasks wait behind the head. |
| `event_preempt` | [event_preempt/README.md](event_preempt/README.md) | `k_event_post()`, which walks the whole wait queue and readies every waiter whose mask matches. The dummy tasks are walked and readied. |
| `event_block` | [event_block/README.md](event_block/README.md) | `k_event_wait_safe()`, in which the measured task pends itself. The dummy tasks wait ahead of it, and its insert walks past them. |
