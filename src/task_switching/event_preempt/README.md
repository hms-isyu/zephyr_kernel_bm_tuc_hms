# event_preempt, preempting switch via `k_event_post()`

Build: set `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_EVENT_PREEMPT=y` in `prj.conf`, one test case
per build. `#define TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h) selects
`TEST_CASE_I` to `TEST_CASE_IV`. The point of the sweep is `#define DUMMY_TASKS_COUNT` in the
`TEST_CASE_III` and `TEST_CASE_IV` branches of that file. Terms, scenarios and shared
configuration: [../README.md](../README.md).

`k_event_post()` sets bits in an event object and wakes every waiting task whose mask the bits
match. This test case times the post by `L_Task_SA` that wakes `H_Task_SA`,
`H_Task_SA > L_Task_SA`, against the post by `H_Task_SB`, which wakes only tasks it outranks,
or none. The post visits every waiter of the wait queue (`z_sched_waitq_walk()`), so dummy
tasks that wait on the posted bit are visited, woken and inserted into the ready queue inside
the window. In [../sem_preempt](../sem_preempt), `k_sem_give()` takes the head of its wait
queue only, and the dummy tasks behind it stay unvisited.

## Participants

| Name | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | creates `H_Task_SA`, `sa_dummy_threads[0..dummy_count-1]` and `L_Task_SA`, resumes `L_Task_SA` once per round after `SIGNALIZE_YIELD_EVENT_MASK`, aborts the `S_A` tasks, creates `H_Task_SB`, `sb_dummy_threads[0..dummy_count-1]` and `L_Task_SB`, resumes `H_Task_SB` once per round. During a measured post it is ready, preempted inside `k_thread_resume()`, and waits in no wait queue. |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 2 | `S_A` | waits in `k_event_wait_safe(SCENARIO_A_EVENT_MASK)`, takes the stop marker when it returns, posts `SIGNALIZE_YIELD_EVENT_MASK`, waits again |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | `S_A` | opens the window, takes the start marker, calls `k_event_post(SCENARIO_A_EVENT_MASK)`, suspends itself |
| `sa_dummy_threads[i]` (`dummy_task`) | `H_Task_SA > D > L_Task_SA` | `S_A` | wait on `SCENARIO_A_EVENT_MASK`. Every `S_A` post wakes them, and each waits again in the loop of `dummy_task` before `L_Task_SA` runs. |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | `S_B` | suspends itself. Resumed, with `TEST_SB_HAS_WAITER == 0` it first resumes `L_Task_SB`, outside the window, then opens the window and takes both markers around `k_event_post(SCENARIO_B_EVENT_MASK)` |
| `L_Task_SB` | `TASK_SB_LOW_PRIO` = 14 | `S_B` | per round posts `SIGNALIZE_YIELD_EVENT_MASK`, then with `TEST_SB_HAS_WAITER == 1` waits on `SCENARIO_B_EVENT_MASK`, with `TEST_SB_HAS_WAITER == 0` suspends itself and is a ready task during the window |
| `sb_dummy_threads[i]` (`dummy_task`) | `H_Task_SB > D > L_Task_SB` | `S_B` | wait on `SCENARIO_B_EVENT_MASK`. Every `S_B` post wakes them, and each waits again before `L_Task_SB` and `I_Task` run. |

Every `TEST_CASE` branch defines the same priorities. `k_event_wait_safe()` waits with
`K_EVENT_OPTION_CLEAR`, so a woken waiter consumes the bit it matched and its next wait pends
again. The measured operation is `k_event_post()` on `test_event`, and in `S_A` the tail of
`k_event_wait_safe()` on the same object.

## Measurement

`k_event_post_internal()` walks the whole wait queue, wakes every match
(`z_sched_wake_thread_locked()`), then calls `z_reschedule()` in every case. `S_A` wakes
`H_Task_SA` and switches to it. `S_B` wakes only tasks `H_Task_SB` outranks, or none.

| Scenario | Trigger | Measurement window | What the window holds |
| --- | --- | --- | --- |
| `S_A` | `L_Task_SA` calls `k_event_post(SCENARIO_A_EVENT_MASK)` | start marker in `L_Task_SA`, stop marker in `H_Task_SA` | the walk over `H_Task_SA` and `dummy_count` dummy tasks, their wake and ready queue insert, the arm of `reschedule()` that calls `z_swap()`, the PendSV exception restoring `H_Task_SA`, the tail of the `k_event_wait_safe()` of `H_Task_SA` |
| `S_B` | `H_Task_SB` calls `k_event_post(SCENARIO_B_EVENT_MASK)` | both markers in `H_Task_SB` | the walk over the waiters of `test_event`, their wake and ready queue insert, the arm of `reschedule()` without `z_swap()`, the tail of `k_event_post()` |

The paths part at the `need_swap()` test in `reschedule()`. Ahead of it the walks differ by test
case. At `CONFIG_SCHED_MULTIQ` = n every ready queue insert passes the ready tasks the woken task
does not outrank; at y every insert is `O(1)`.

| Test case | `TEST_SB_HAS_WAITER` | `ADD_DUMMY_TASKS` | `S_B` walks | Ahead of `need_swap()`, `S_A - S_B` holds |
| --- | --- | --- | --- | --- |
| `TEST_CASE_I` | 1 | 0 | `L_Task_SB` | n: one insert step less in `S_A`. y: nothing. |
| `TEST_CASE_II` | 0 | 0 | no waiter | one walk step with the wake and insert of `H_Task_SA` |
| `TEST_CASE_III` | 1 | 1 | `dummy_count` dummy tasks, then `L_Task_SB` | n: `dummy_count + 1` insert steps less in `S_A`, since `L_Task_SB` is inserted behind every woken dummy task. y: nothing. |
| `TEST_CASE_IV` | 0 | 1 | `dummy_count` dummy tasks | one walk step with the wake and insert of `H_Task_SA`. The dummy task inserts pass the same number of tasks in both scenarios. |

The sweep parameter is `DUMMY_TASKS_COUNT`, swept one build per value over `0..DUMMY_POOL_SIZE`
in the `TEST_CASE_III` and `TEST_CASE_IV` branches. At `DUMMY_TASKS_COUNT` = 0, `TEST_CASE_III`
creates the same tasks as `TEST_CASE_I`, and `TEST_CASE_IV` the same as `TEST_CASE_II`.

`S_A` runs first. Its first round runs when `L_Task_SA` is created, so `I_Task` resumes
`L_Task_SA` `MEASUREMENT_COUNT - 1` times. `H_Task_SB` suspends itself before each round, so
`I_Task` resumes it `MEASUREMENT_COUNT` times.

## Compensation

| Probe | Series | Re-runs without the switch | May be set against |
| --- | --- | --- | --- |
| `measure_event_z_swap_overhead(SIM_Z_SWAP_PROLOGUE)` | `z_swap_overhead` | the `z_swap()` prologue, up to the `irq_unlock()` in `arch_swap()` | the `S_A` side after the `need_swap()` test |
| `measure_wait_overhead(SIM_WAIT_TAIL)` | `wait_tail` | the tail of `k_event_wait_safe()` | the `S_A` side after the PendSV exception |
| `measure_event_tail_overhead()` | `event_tail` | the tail of `k_event_post()` on the arm of `reschedule()` without `z_swap()` | the `S_B` side after the `need_swap()` test |

No probe re-runs the PendSV exception itself.

## Analysis

Documents, each an instruction listing of the windows from the linked `zephyr.elf`, built at
`CONFIG_SCHED_MULTIQ` = n: `sa_vs_sb_case_<case>.html` for one test case,
`_d1`..`_d3` for `dummy_count` = 1..3, `case_I_III` and `case_II_IV` for the complete window
at `TEST_SB_HAS_WAITER` = 1 and 0, `sim_vs_kernel.html` for each probe against the kernel code
it re-runs: [doc/sa_vs_sb_case_I.html](doc/sa_vs_sb_case_I.html),
[doc/sa_vs_sb_case_II.html](doc/sa_vs_sb_case_II.html),
[doc/sa_vs_sb_case_I_III.html](doc/sa_vs_sb_case_I_III.html),
[doc/sa_vs_sb_case_II_IV.html](doc/sa_vs_sb_case_II_IV.html),
[doc/sa_vs_sb_case_III_d1.html](doc/sa_vs_sb_case_III_d1.html),
[doc/sa_vs_sb_case_III_d2.html](doc/sa_vs_sb_case_III_d2.html),
[doc/sa_vs_sb_case_III_d3.html](doc/sa_vs_sb_case_III_d3.html),
[doc/sa_vs_sb_case_IV_d1.html](doc/sa_vs_sb_case_IV_d1.html),
[doc/sa_vs_sb_case_IV_d2.html](doc/sa_vs_sb_case_IV_d2.html),
[doc/sa_vs_sb_case_IV_d3.html](doc/sa_vs_sb_case_IV_d3.html),
[doc/sim_vs_kernel.html](doc/sim_vs_kernel.html).

Expected of the base measurement: `S_A - S_B` is the `z_swap()` prologue, the PendSV exception
and the tail of `k_event_wait_safe()`, less the tail of `k_event_post()` on the arm without
`z_swap()`, plus the term of the table above.

Expected of the loaded measurement: `scenario_a` and `scenario_b` each carry a term in
`DUMMY_TASKS_COUNT`, `O(dummy_count)` for the walk and the wakes, and at `CONFIG_SCHED_MULTIQ`
= n `O(dummy_count²)` for the inserts, since the `k`-th woken dummy task passes `k + 1` ready
tasks. `S_A - S_B` is expected to carry a term in `DUMMY_TASKS_COUNT` only in `TEST_CASE_III` at
`CONFIG_SCHED_MULTIQ` = n.

## Configuration

| Option | Value | Consequence special to this test case |
| --- | --- | --- |
| `CONFIG_WAITQ_SIMPLE` | y, Kconfig default | the post visits every waiter in priority order; `event_walk_op()` never ends the walk early |
| `CONFIG_SCHED_MULTIQ` | n in `prj.conf`, y a separate build | the woken dummy tasks are inserted into the ready queue inside both windows, so this option decides the order of the load term in each series |
