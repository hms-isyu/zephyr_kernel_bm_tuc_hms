# task_change, same priority switch via `k_yield()`

Link this test by setting `CONFIG_BENCHMARK_TEST_TASK_SWITCH_CHANGE=y` in `prj.conf` (Kconfig
choice `BENCHMARK_TEST`, one test case per build) and rebuilding. Terminology:
[../README.md](../README.md).

## General

`L` and `H` sit in the same band, so the scheduler has no preemption decision to make. The
switch happens only because the running task hands the CPU on with `k_yield()`, which rotates
the caller to the tail of its own band and picks the new head.

| Scenario | Trigger | Measurement window | Measured path |
| --- | --- | --- | --- |
| A | Transition | start marker in `L_Task_SA`, stop marker in `H_Task_SA` (cross-thread) | `k_yield()` entry, band rotation, `reschedule()`, the swap, and the return of `H_Task_SA`'s own `k_yield()` |
| B | Activation API | start and stop marker inside `Task_SB` (same function) | `k_yield()` on a task that is alone in the highest occupied band: rotation and `reschedule()` run, the queue head does not change, no swap happens |

`S_A - S_B` isolates the context switch itself, the yield API and the scheduler decision
cancel out.

The Kconfig prompt for this choice reads *"Task switch via `k_thread_priority_set()`"*. The
code triggers the switch with `k_yield()` between tasks that already share a priority.

## Threads

| Thread | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | creates and tears down the measured tasks, then spins on the result signal |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 3 | A | opens the window, takes the start marker, yields |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 3 | A | takes the stop marker when its `k_yield()` returns |
| `sa_dummy_threads[0..n-1]` | `TASK_SA_DUMMY_1..4_PRIO`, `D == L == H` | A | band load, also close the window and own the termination check when present |
| `Task_SB` | `TASK_SB_HIGH_PRIO` = 2 | B | yields with nothing else in its band |
| kernel idle | 16 | both | runs only when no test task is ready |

`D == L == H`, the only test in the family where the dummies share the band of the measured
pair. `sb_dummy_threads` / `TASK_SB_DUMMY_*_PRIO`
(10..13) are allocated and defined but never created. The `L_` / `H_` names are kept from the
preempting tests, here both are at priority 3.

## Measurement

Selected by `TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h).

| Case | Priorities (L / H, S_B) | Dummy load | Measures |
| --- | --- | --- | --- |
| `TEST_CASE_I` | 3 / 3, 2 | none | base measurement: the bare `L -> H` yield switch, two tasks in the band |
| `TEST_CASE_II` | 3 / 3, 2 | `n` tasks with `D == L == H` | loaded measurement: the same switch with the band loaded |

`DUMMY_TASKS_COUNT` is the sweep parameter of the loaded measurement and is changed between
runs, one build per value, `n` = 1..4 (capped by `DUMMY_POOL_SIZE`). The value committed in
[benchmark_testcases.h](benchmark_testcases.h) is only the point that was measured last.

The base measurement holds band 3 at two tasks. The loaded measurement adds `n` dummies to
the same band and changes nothing else. With the dummy load on, `H_Task_SA` skips its
`BMTH_mwindow_close()` and termination check (`if (DUMMY_TASKS_COUNT == 0U)`) and `dummy_task`
takes both over, so the closing task is always the one that ran last before `L_Task_SA`
re-opens the window.

The two scenarios run one after the other: `I_Task` creates the S_A tasks, releases them with
`START_EVENT_MASK`, and only after they have suspended themselves does it abort them and
create `Task_SB`.

## Compensation

None. This test carries no `zephyr_sim.c`, there are no simulated half chains to subtract.

## Analysis

Documents: [doc/sa_vs_sb.html](doc/sa_vs_sb.html).

Queue state at the start marker, the kernel idle task (16) is ready in every row and left out.
`SA_D1`…`SA_Dn` are `sa_dummy_threads[]` in pool order, `n` = `DUMMY_TASKS_COUNT`.

| Case | Scenario | Releasing task | Ready queue | Wait queue | After the measured call |
| --- | --- | --- | --- | --- | --- |
| I | A | `L_Task_SA` (3) | `H_Task_SA` (3), `I_Task` (15) | empty | `k_yield()` rotates `L_Task_SA` to the tail of band 3, the head becomes `H_Task_SA`, swap, stop marker in `H_Task_SA` |
| I | B | `Task_SB` (2) | `I_Task` (15) | empty | `k_yield()` rotates `Task_SB` inside band 2, which stays the head, no swap, stop marker in `Task_SB` |
| II | A | `L_Task_SA` (3) | `H_Task_SA` (3), `SA_D1` (3) … `SA_Dn` (3), `I_Task` (15) | empty | as case I, with `n` extra tasks queued in band 3 behind `H_Task_SA` |
| II | B | `Task_SB` (2) | `I_Task` (15) | empty | as case I, the dummy load exists on the S_A side only |

How the dummies load the workload:

- The load lands in the **ready queue**, in band 3, behind `H_Task_SA`. The order is
  `L_Task_SA`, `H_Task_SA`, `SA_D1` … `SA_Dn`, filled by the `START_EVENT_MASK` post, which
  walks the event wait queue in priority order and, at equal priority, in insertion order.
- The **wait queue** is empty in both scenarios. `test_event` is used only outside the
  measurement window: `START_EVENT_MASK` (0x0008) as the start barrier, on which the whole S_A
  set pends once before the first iteration. `SCENARIO_A_EVENT_MASK` (0x0001) and
  `SIGNALIZE_YIELD_EVENT_MASK` (0x0004) are defined but never posted.
- One rotation of band 3 is one round:
  `L_Task_SA -> H_Task_SA -> SA_D1 -> … -> SA_Dn -> L_Task_SA`. Only the
  `L_Task_SA -> H_Task_SA` hand-off carries the markers, the dummies are band occupancy alone.
- Under `SCHED_MULTIQ` rotation and "pick best" are O(1), so the band occupancy swept here is
  expected to leave the measured value unchanged.

## Configuration

Shared configuration: [../README.md](../README.md). Nothing in this test deviates from it.

The result is specific to `CONFIG_SCHED_MULTIQ`. With `CONFIG_SCHED_SIMPLE` in its place the
ready queue is a single sorted list, `k_yield()` re-inserts by walking forward from the
yielding task's position, and the band occupancy swept here enters the measured path.
