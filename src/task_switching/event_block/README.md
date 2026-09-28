# event_block, blocking switch via `k_event_wait_safe()`

Set `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_EVENT_BLOCK=y` in `prj.conf`.

[../README.md](../README.md)

`k_event_wait_safe()` blocks the calling task until the requested bits of an event object are
set, unless the timeout is `K_NO_WAIT`, in which case it returns at once. This test case times
the wait by `H_Task_SA`, which blocks and hands the CPU to `L_Task_SA`, against the wait by
`H_Task_SB` with `K_NO_WAIT`, which returns.

## Participants

| Name | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | control task |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 6 | `S_A` | takes the start marker, calls `k_event_wait_safe(SCENARIO_A_EVENT_MASK, false, K_FOREVER)` |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | `S_A` | takes the stop marker, posts `SCENARIO_A_EVENT_MASK` |
| `sa_dummy_threads[i]` (`dummy_task`) | `D > H_Task_SA` | `S_A` | wait on `SCENARIO_A_EVENT_MASK` ahead of `H_Task_SA` in the wait queue of `test_event` |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | `S_B` | takes both markers around `k_event_wait_safe(SCENARIO_B_EVENT_MASK, false, K_NO_WAIT)` |

The measured operation is `k_event_wait_safe()` on `test_event`; `S_A` also times the tail of
the `k_event_post()` of `L_Task_SA` on the same object.

## Measurement

`H_Task_SA` calls `k_event_wait_safe(SCENARIO_A_EVENT_MASK, false, K_FOREVER)` while
`SCENARIO_A_EVENT_MASK` is clear. `H_Task_SB` calls `k_event_wait_safe(SCENARIO_B_EVENT_MASK,
false, K_NO_WAIT)` while `SCENARIO_B_EVENT_MASK` is clear.

| Scenario | Measurement window | What the window holds |
| --- | --- | --- |
| `S_A` | start marker in `H_Task_SA`, stop marker in `L_Task_SA` | the insert of `H_Task_SA` into the wait queue of `test_event` behind `dummy_count` dummy tasks, the switch, the tail of the `k_event_post()` of `L_Task_SA` |
| `S_B` | both markers in `H_Task_SB` | the call up to the `K_NO_WAIT` test, the tail of `k_event_wait_safe()` |

The two paths part where the call tests `K_NO_WAIT`: `S_A` continues past it, `S_B` unlocks and
returns. `S_A - S_B` isolates the insert of `H_Task_SA`, the switch and the tail of
`k_event_post()`; the dummy tasks wait in `test_event` throughout the window, so the load
lengthens the insert alone.

| Test case | `ADD_DUMMY_TASKS` in the branch | Dummy tasks | Measures |
| --- | --- | --- | --- |
| `TEST_CASE_I` | 0 | none | base measurement |
| `TEST_CASE_II` | 1 | `sa_dummy_threads[0..dummy_count-1]`, `D > H_Task_SA` | loaded measurement |

The sweep parameter is `DUMMY_TASKS_COUNT`, swept one build per value over `0..DUMMY_POOL_SIZE`
in the `TEST_CASE_II` branch. No script runs the sweep: before each build,
set `#define DUMMY_TASKS_COUNT` in that branch of `benchmark_testcases.h` by hand.
At `DUMMY_TASKS_COUNT` = 0, `TEST_CASE_II` creates the same tasks
as `TEST_CASE_I`. Each dummy task ahead of `H_Task_SA` adds one step to its insert into the wait
queue of `test_event`, `O(dummy_count)`.

`I_Task` resumes `L_Task_SA` `MEASUREMENT_COUNT` times, then removes the `S_A` tasks, creates
`H_Task_SB`, and resumes it `MEASUREMENT_COUNT - 1` times.

## Compensation

| Simulated | Function |
| --- | --- |
| prologue, up to the `irq_unlock()` where PendSV would fire | `arch_swap()` |
| tail, from the `K_NO_WAIT` test to return | `k_event_wait_safe()` |

## Analysis

Documents, each an instruction listing from the linked `zephyr.elf`:
[doc/sa_vs_sb.html](doc/sa_vs_sb.html), both windows in execution order at an empty wait queue;
[doc/sim_vs_kernel.html](doc/sim_vs_kernel.html), the code of each simulated part against the
kernel code it re-runs.

## Configuration

General configuration: [../README.md](../README.md#configuration).

| Option | Meaning |
| --- | --- |
| `CONFIG_WAITQ_SIMPLE` | Kconfig default; selects a linked-list insert for `H_Task_SA` into the wait queue of `test_event`, so the loaded measurement's insert term depends on `DUMMY_TASKS_COUNT` |
