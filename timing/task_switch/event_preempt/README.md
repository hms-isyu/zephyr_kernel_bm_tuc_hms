# event_preempt, preempting switch via `k_event_post()`

Link this test by setting `CONFIG_BENCHMARK_TEST_EVENT_TASK_SWITCH_PREEMPT=y` in `prj.conf`
(Kconfig choice `BENCHMARK_TEST`, one test case per build) and rebuilding. Terminology:
[../README.md](../README.md).

## General

The switch is triggered from a wait queue. `H` is already pending in `k_event_wait_safe()`
when `L` posts the bit it waits for. `k_event_post()` walks the *entire* wait queue of the
object (`z_sched_waitq_walk()`), tests every waiter mask, readies each waiter that matches,
and calls `z_reschedule()` once at the end of the walk.

| Scenario | Trigger | Measurement window | Measured path |
| --- | --- | --- | --- |
| A | Transition | start marker in `L_Task_SA`, stop marker in `H_Task_SA` (cross-thread) | `k_event_post()` entry, the walk over the whole wait queue, the ready-add of every matched waiter, `z_reschedule()`, the swap, and the tail of `k_event_wait_safe()` in `H_Task_SA` |
| B | Activation API | start and stop marker inside `H_Task_SB` (same function) | the same post where no matched waiter can preempt the poster: entry, walk, ready-adds, `z_reschedule()` on the no-swap arm, return |

`S_A - S_B` leaves the swap and the wait tail when S_B has a waiter
(`TEST_SB_HAS_WAITER == 1`), and additionally the walk and the ready-adds when it has none
(`TEST_SB_HAS_WAITER == 0` with no dummy load, where the post never reaches the scheduler).

`k_event_wait_safe()` is the consuming variant (`K_EVENT_OPTION_CLEAR`): a woken waiter clears
the bits it matched, so every round starts from the same event state and the waiters re-pend
on a bit that is not set.

## Threads

| Thread | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | consumes `SIGNALIZE_YIELD_EVENT_MASK` and resumes the driver task once per round, tears the scenarios down |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 2 | A | pends on `SCENARIO_A_EVENT_MASK`, takes the stop marker when the wait returns |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | A | takes the start marker, posts `SCENARIO_A_EVENT_MASK`, is preempted, then self-suspends |
| `sa_dummy_threads[0..n-1]` (`dummy_task`) | `TASK_SA_DUMMY_1..4_PRIO`, one band apart, `H > D > L` | A | wait-queue load, each pends on `SCENARIO_A_EVENT_MASK`, so each is matched, woken and re-pended every round |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | B | posts `SCENARIO_B_EVENT_MASK` and takes both markers, self-suspends at the end of each round |
| `L_Task_SB` | `TASK_SB_LOW_PRIO` = 14 | B | with `TEST_SB_HAS_WAITER == 1` pends on `SCENARIO_B_EVENT_MASK`, otherwise self-suspends and is resumed explicitly |
| `sb_dummy_threads[0..n-1]` (`dummy_task`) | `TASK_SB_DUMMY_1..4_PRIO`, one band apart, `H_Task_SB > D > L_Task_SB` | B | wait-queue load pending on `SCENARIO_B_EVENT_MASK` |
| kernel idle | 16 | both | runs only when no test task is ready |

On the S_A side `H > D > L`, so the dummies are enqueued behind the measured waiter while each
of them still outranks the poster. On the S_B side `H_Task_SB > D > L_Task_SB`, because the
post wakes every matching waiter and the queue order does not decide which task runs.

## Measurement

Selected by `TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h). The priorities are
the same in all four cases (S_A 7 / 2, S_B 8 / 14).

| Case | `TEST_SB_HAS_WAITER` | `DUMMY_TASKS_COUNT` | Measures |
| --- | --- | --- | --- |
| `TEST_CASE_I` | 1 | 0 | base measurement: switch and wait tail, against a post that walks one waiter and does not switch |
| `TEST_CASE_II` | 0 | 0 | base measurement, wider difference: switch, wait tail, walk and ready-add, against a post that reaches nothing |
| `TEST_CASE_III` | 1 | `n` | loaded measurement of case I, `n` extra waiters on both sides |
| `TEST_CASE_IV` | 0 | `n` | loaded measurement: walk and ready-add cost against waiter count, with no switch on the S_B side |

`DUMMY_TASKS_COUNT` is the sweep parameter of the loaded measurement and is changed between
runs, one build per value, `n` = 1..4 (capped by `DUMMY_POOL_SIZE`). The value committed in
[benchmark_testcases.h](benchmark_testcases.h) is only the point that was measured last.

The scenarios run one after the other: `I_Task` drives S_A for `MEASUREMENT_COUNT` iterations,
aborts the S_A tasks, then creates the S_B set and drives it the same way.

## Compensation

Measured before the scenarios start, in `main()`, from [zephyr_sim.c](zephyr_sim.c). Every
simulated chain is cut at the `irq_unlock()` where the kernel pends PendSV and the task leaves
the CPU (see [sim_measure.h](sim_measure.h)).

| Probe | Series | What it re-runs without a switch |
| --- | --- | --- |
| `measure_event_tail_overhead()` | `event_tail` | the tail of `k_event_post()` for a post with no matching waiter |
| `measure_wait_overhead(SIM_WAIT_TAIL)` | `wait_tail` | the half of `k_event_wait_safe()` after the PendSV point, where the waiter gets the CPU back |
| `measure_event_z_swap_overhead(SIM_Z_SWAP_PROLOGUE)` | `z_swap_overhead` | the swap chain from entry to the `irq_unlock()` where PendSV would fire |

## Analysis

Documents: [doc/sa_vs_sb_case_I.html](doc/sa_vs_sb_case_I.html),
[doc/sa_vs_sb_case_II.html](doc/sa_vs_sb_case_II.html),
[doc/sa_vs_sb_case_I_III.html](doc/sa_vs_sb_case_I_III.html),
[doc/sa_vs_sb_case_II_IV.html](doc/sa_vs_sb_case_II_IV.html),
[doc/sa_vs_sb_case_III_d1.html](doc/sa_vs_sb_case_III_d1.html),
[doc/sa_vs_sb_case_III_d2.html](doc/sa_vs_sb_case_III_d2.html),
[doc/sa_vs_sb_case_III_d3.html](doc/sa_vs_sb_case_III_d3.html),
[doc/sa_vs_sb_case_IV_d1.html](doc/sa_vs_sb_case_IV_d1.html),
[doc/sa_vs_sb_case_IV_d2.html](doc/sa_vs_sb_case_IV_d2.html),
[doc/sa_vs_sb_case_IV_d3.html](doc/sa_vs_sb_case_IV_d3.html),
[doc/sim_vs_kernel.html](doc/sim_vs_kernel.html). The `_d1`…`_d3` suffix is the dummy count
inside the loaded case.

Queue state at the start marker, the kernel idle task (16) is ready in every row and left out.
The wait queue is `test_event` in queue order (priority sorted). `SA_D1`…`SA_Dn` are
`sa_dummy_threads[]` (3, 4, 5, 6) and `SB_D1`…`SB_Dn` are `sb_dummy_threads[]`
(10, 11, 12, 13), each created in pool order. Cases III and IV are shown at `n` = 4, the top
of the sweep; the walk length and the number of ready-adds follow `n`.

| Case | Scenario | Posting task | Ready queue | Wait queue (`test_event`) | After the measured call |
| --- | --- | --- | --- | --- | --- |
| I | A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2) | walks 1 node, wakes `H_Task_SA` into band 2, swap, stop marker in `H_Task_SA` |
| I | B | `H_Task_SB` (8) | `I_Task` (15) | `L_Task_SB` (14) | walks 1 node, wakes `L_Task_SB` into band 14, poster > woken task, no swap, stop marker in `H_Task_SB` |
| II | A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2) | as case I |
| II | B | `H_Task_SB` (8) | `L_Task_SB` (14), `I_Task` (15) | empty | no waiter at all: no walk, no ready-add, the scheduler is never reached, stop marker in `H_Task_SB` |
| III | A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2), `SA_D1` (3) … `SA_Dn` | walks `n`+1 nodes, wakes all of them into their bands, swap to band 2, stop marker in `H_Task_SA` |
| III | B | `H_Task_SB` (8) | `I_Task` (15) | `SB_D1` (10) … `SB_Dn`, `L_Task_SB` (14) | walks `n`+1 nodes, wakes all of them, poster > every woken task, no swap, stop marker in `H_Task_SB` |
| IV | A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2), `SA_D1` (3) … `SA_Dn` | walks `n`+1 nodes, wakes all of them into their bands, swap to band 2, stop marker in `H_Task_SA` |
| IV | B | `H_Task_SB` (8) | `L_Task_SB` (14), `I_Task` (15) | `SB_D1` (10) … `SB_Dn` | walks `n` nodes, wakes all of them, poster > every woken task, no swap, stop marker in `H_Task_SB` |

How the dummies load the workload:

- The load lands in the **wait queue** of `test_event`. Before the measured call every dummy
  holds a node there, after it every dummy has been moved into its own band in the **ready
  queue**. Both the walk length and the number of ready-adds therefore scale with
  `DUMMY_TASKS_COUNT`. This is the difference to [../sem_preempt](../sem_preempt), where
  `k_sem_give()` pops the head only.
- `z_reschedule()` runs once, after the walk, so all ready-adds are already inside the
  measurement window when the swap decision is taken.
- With `TEST_SB_HAS_WAITER == 0` (cases II and IV) `L_Task_SB` does not pend, `H_Task_SB`
  resumes it before opening the window, which is why it appears in the ready queue there.
- `I_Task` is *ready*, not pending, in every row: it is preempted inside `k_thread_resume()`
  and finds `SIGNALIZE_YIELD_EVENT_MASK` already set on its next pass, so it holds no node in
  the wait queue while the measured post walks it.

`test_event` carries every signal in the test:

| Mask | Direction | In the measurement window |
| --- | --- | --- |
| `SCENARIO_A_EVENT_MASK` (0x0001) | `L_Task_SA` to `H_Task_SA` and the S_A dummies | yes, the measured post of scenario A |
| `SCENARIO_B_EVENT_MASK` (0x0002) | `H_Task_SB` to `L_Task_SB` and the S_B dummies | yes, the measured post of scenario B |
| `SIGNALIZE_YIELD_EVENT_MASK` (0x0004) | driver task to `I_Task` | no, posted after the window closes |
| `START_EVENT_MASK` (0x0008) | start barrier | no, before the first iteration |

Round order in scenario A: `I_Task` resumes `L_Task_SA` and is preempted, `L_Task_SA` posts,
`H_Task_SA` (band 2) runs first, closes the window, posts `SIGNALIZE_YIELD_EVENT_MASK` and
re-pends, the dummies (bands 3..6) run and re-pend, `L_Task_SA` returns from the post and
self-suspends, `I_Task` regains the CPU and resumes `L_Task_SA` again.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option (`prj.conf`) | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_EVENTS` | y | `k_event` available |
| `CONFIG_WAITQ_SIMPLE` | y | `test_event` owns one priority-sorted list (`z_priq_simple_*`), a pend walks it from the head, the measured post walks all of it |
