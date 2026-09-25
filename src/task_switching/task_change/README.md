# task_change, same priority switch via `k_yield()`

Build: set `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_TASK_CHANGE=y` in `prj.conf`, one test case
per build. `#define TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h) selects
`TEST_CASE_I` or `TEST_CASE_II`. The point of the sweep is `#define DUMMY_TASKS_COUNT` in the
`TEST_CASE_II` branch of that file. Terms, scenarios and shared configuration:
[../README.md](../README.md).

`k_yield()` moves the calling task behind the other ready tasks of its priority and hands the
CPU to the first of them. This test case times `k_yield()` from `L_Task_SA` to `H_Task_SA`, two
tasks of equal priority, against `k_yield()` by `Task_SB`, the only task at its priority.
`z_impl_k_yield()` calls `z_swap()` unconditionally, so both scenarios take the PendSV
exception: in `S_A` it restores `H_Task_SA`, in `S_B` it restores `Task_SB`. `S_A - S_B`
therefore holds no context switch code, only the difference in the re-insert of the yielding
task. In [../task_preempt](../task_preempt) `S_B` returns without `z_swap()` and only `S_A`
holds the PendSV exception.

## Participants

| Name | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | creates `L_Task_SA`, `H_Task_SA` and `sa_dummy_threads[0..dummy_count-1]`, posts `START_EVENT_MASK`, aborts them once all have suspended themselves, then creates `Task_SB` |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 3 | `S_A` | opens the window, takes the start marker, calls `k_yield()` |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 3 | `S_A` | takes the stop marker when its own `k_yield()` returns, then calls `k_yield()` again |
| `sa_dummy_threads[i]` (`dummy_task`) | `D == L_Task_SA == H_Task_SA` | `S_A` | ready tasks at the priority of the measured pair. Each calls `BMTH_mwindow_close()` and `k_yield()` per round. |
| `Task_SB` | `TASK_SB_HIGH_PRIO` = 2 | `S_B` | opens the window, takes both markers around `k_yield()`. `Task_SB > I_Task`, and no other task is ready. |

The measured operation is `k_yield()` in both scenarios. It works on the ready queue only.
`sb_dummy_threads[]` is never created.

## Measurement

In `S_A` the ready tasks at priority 3 stand in the order `L_Task_SA`, `H_Task_SA`,
`sa_dummy_threads[0..dummy_count-1]`, followed by `I_Task`. `L_Task_SA` yields to `H_Task_SA`,
the task directly behind it. In `S_B` `Task_SB` yields with only `I_Task` behind it and gets
the CPU back.

| Scenario | Trigger | Measurement window | What the window holds |
| --- | --- | --- | --- |
| `S_A` | `L_Task_SA` calls `k_yield()` | start marker in `L_Task_SA`, stop marker in `H_Task_SA` | `k_yield()` of `L_Task_SA` up to `z_swap()`, the PendSV exception restoring `H_Task_SA`, the tail of the `k_yield()` of `H_Task_SA` |
| `S_B` | `Task_SB` calls `k_yield()` | both markers in `Task_SB` | `k_yield()` of `Task_SB` up to `z_swap()`, the PendSV exception restoring `Task_SB`, the tail of the same `k_yield()` |

Both windows run `z_impl_k_yield()`, `update_cache(1)`, `z_swap()` and `z_arm_pendsv` on the
same branches. They differ in the re-insert of the yielding task only:

| `CONFIG_SCHED_MULTIQ` | Re-insert | `S_A - S_B` |
| --- | --- | --- |
| n | `z_priq_simple_yield()` walks on from the yielding task past every task `==` it and stops at `I_Task`, the first task it outranks. `S_A` visits `H_Task_SA`, `dummy_count` dummy tasks and `I_Task`, `S_B` visits `I_Task`. | `dummy_count + 1` steps of that walk, `O(dummy_count)` |
| y | `z_priq_mq_yield()` removes the yielding task and appends it to the list of its priority, `O(1)` in both scenarios. | no difference in the kernel path |

| Test case | Parameters | Measures |
| --- | --- | --- |
| `TEST_CASE_I` | `ADD_DUMMY_TASKS` undefined, `dummy_count` = 0 | base measurement |
| `TEST_CASE_II` | `ADD_DUMMY_TASKS` = 1, `DUMMY_TASKS_COUNT` dummy tasks with `D == L_Task_SA == H_Task_SA` | loaded measurement |

The sweep parameter is `DUMMY_TASKS_COUNT`, swept one build per value over `0..DUMMY_POOL_SIZE`
in the `TEST_CASE_II` branch. At `DUMMY_TASKS_COUNT` = 0, `TEST_CASE_II` creates the same tasks
as `TEST_CASE_I`. `H_Task_SA` tests the macro `DUMMY_TASKS_COUNT == 0U`, not `dummy_count`: at
`DUMMY_TASKS_COUNT` = 0 it closes the window after its stop marker and ends `S_A` at
`MEASUREMENT_COUNT`. At `DUMMY_TASKS_COUNT` > 0 the compiler drops that code from `H_Task_SA`,
and `sa_dummy_threads[0]`, the first task to run after `H_Task_SA` yields, closes the window
and ends `S_A`. No dummy task runs inside the window, because `H_Task_SA` stands directly
behind `L_Task_SA`.

`S_A` runs first. `I_Task` runs only when no task at priority 3 is ready, so it aborts the
`S_A` tasks after they have suspended themselves at `MEASUREMENT_COUNT`, then creates
`Task_SB`.

## Analysis

[doc/sa_vs_sb.html](doc/sa_vs_sb.html): every instruction of both windows, in execution order,
from the linked `zephyr.elf`.

Expected of the base measurement: `scenario_a` and `scenario_b` each hold a full PendSV
exception. `S_A - S_B` is expected to be one walk step at `CONFIG_SCHED_MULTIQ` = n and to show
no kernel path difference at y.

Expected of the loaded measurement: at `CONFIG_SCHED_MULTIQ` = n, `scenario_a` and
`S_A - S_B` are expected to carry a term in `DUMMY_TASKS_COUNT`, `scenario_b` none, since
`Task_SB` runs without dummy tasks. At y, neither series is expected to carry one.

## Configuration

| Option | Value | Consequence special to this test case |
| --- | --- | --- |
| `CONFIG_SCHED_MULTIQ` | n in `prj.conf`, y a separate build | selects the re-insert of `k_yield()`, and with it whether the loaded measurement depends on `DUMMY_TASKS_COUNT` |
| `CONFIG_USE_SWITCH` | n, resolved | `k_yield()` has no path without the PendSV exception, so `S_B` holds it as well |
