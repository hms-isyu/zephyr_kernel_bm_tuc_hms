# task_change, same priority switch via `k_yield()`

Set `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_TASK_CHANGE=y` in `prj.conf`.

[../README.md](../README.md)

`k_yield()` moves the calling task behind the other ready tasks of its priority and hands the
CPU to the first of them. This test case times `k_yield()` called by `L_Task_SA`, which shares
its priority with a ready `H_Task_SA`, against the same call by `Task_SB`, the only task at its
own priority.

## Participants

| Name | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | control task |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 3 | `S_A` | takes the start marker, calls `k_yield()` |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 3 | `S_A` | takes the stop marker on return from its own `k_yield()` |
| `sa_dummy_threads[i]` (`dummy_task`) | `D == L_Task_SA == H_Task_SA` | `S_A` | ready tasks at the priority of the measured pair, each calling `k_yield()` once per iteration cycle |
| `Task_SB` | `TASK_SB_HIGH_PRIO` = 2 | `S_B` | takes both markers around `k_yield()` |

The measured operation is `k_yield()`, acting on the ready queue.

## Measurement

`L_Task_SA` calls `k_yield()` while `H_Task_SA` stands next behind it in the ready queue at
priority 3; `Task_SB` calls the same operation as the only ready task at priority 2.

| Scenario | Measurement window | What the window holds |
| --- | --- | --- |
| `S_A` | start marker in `L_Task_SA`, stop marker in `H_Task_SA` | the re-insert of `L_Task_SA`, the switch, the tail of the `k_yield()` of `H_Task_SA` |
| `S_B` | both markers in `Task_SB` | the re-insert of `Task_SB`, the switch, the tail of the same `k_yield()` |

Both windows take the same switch; they differ only in the re-insert of the yielding task into
the ready queue. At `CONFIG_SCHED_MULTIQ` = n the re-insert walks past every ready task `==` the
yielding task, so `S_A - S_B` holds `dummy_count + 1` more steps of that walk, `O(dummy_count)`;
at y the re-insert is `O(1)` in both scenarios.

Set `#define TEST_CASE` to one of these two values in `benchmark_testcases.h`:

| Test case | Parameters | Measures |
| --- | --- | --- |
| `TEST_CASE_I` | `ADD_DUMMY_TASKS` undefined, `dummy_count` = 0 | base measurement |
| `TEST_CASE_II` | `ADD_DUMMY_TASKS` = 1, `DUMMY_TASKS_COUNT` dummy tasks with `D == L_Task_SA == H_Task_SA` | loaded measurement |

The sweep parameter is `DUMMY_TASKS_COUNT`, swept one build per value over `0..DUMMY_POOL_SIZE`
in the `TEST_CASE_II` branch. No script runs the sweep: before each build,
set `#define DUMMY_TASKS_COUNT` in that branch of `benchmark_testcases.h` by hand.
At `DUMMY_TASKS_COUNT` = 0, `TEST_CASE_II` creates the same tasks
as `TEST_CASE_I`. The dummy tasks act on `S_A` only; `S_B` creates none at any point of the
sweep, and no dummy task runs inside either window, because `H_Task_SA` stands directly behind
`L_Task_SA`.

`I_Task` removes `L_Task_SA`, `H_Task_SA` and the dummy tasks only after they have suspended
themselves at `MEASUREMENT_COUNT`, then creates `Task_SB`.

## Compensation

None: this test case simulates no prologue and no tail.

## Analysis

[doc/sa_vs_sb.html](doc/sa_vs_sb.html): every instruction of both windows, in execution order,
from the linked `zephyr.elf`.

## Configuration

General configuration: [../README.md](../README.md#configuration).

| Option | Meaning |
| --- | --- |
| `CONFIG_SCHED_MULTIQ` | selects the re-insert of `k_yield()`, and with it whether the loaded measurement depends on `DUMMY_TASKS_COUNT` |
| `CONFIG_USE_SWITCH` | `k_yield()` has no path without the switch, so `S_B` holds it as well |
