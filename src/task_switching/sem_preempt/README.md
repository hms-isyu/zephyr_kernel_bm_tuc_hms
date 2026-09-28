# sem_preempt, preempting switch via `k_sem_give()`

Set `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_SEM_PREEMPT=y` in `prj.conf`.

[../README.md](../README.md)

`k_sem_give()` hands the semaphore to the first task in its wait queue and makes it ready, or,
with the wait queue empty, increments the count. This test case times `k_sem_give()` by
`L_Task_SA` to `H_Task_SA` waiting in `k_sem_take()`, `H_Task_SA > L_Task_SA`, against the
give by `H_Task_SB`, which finds either a waiter it outranks or no waiter. The give takes the
head of the wait queue only, so dummy tasks waiting behind the head are not visited; in
`event_preempt`, `k_event_post()` visits every waiter of its wait queue.

## Participants

| Name | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | outranked by every task in this table | both | creates the task set of each scenario, drives the iteration cycles |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 2 | `S_A` | takes the stop marker on return from `k_sem_take(K_FOREVER)` |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | `S_A` | takes the start marker, calls `k_sem_give()` |
| `sa_dummy_threads[i]` (`dummy_task`) | `H_Task_SA > D > L_Task_SA` | `S_A` | wait in `k_sem_take(K_FOREVER)` behind `H_Task_SA`, never woken |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | `S_B` | takes both markers around `k_sem_give()` |
| `L_Task_SB` | `TASK_SB_LOW_PRIO` = 10 | `S_B` | waits in `k_sem_take(K_FOREVER)` at the head of the wait queue when `TEST_SB_HAS_WAITER` = 1, otherwise suspended outside the window |
| `sb_dummy_threads[i]` (`dummy_task`) | `H_Task_SB > L_Task_SB > D` | `S_B` | wait in `k_sem_take(K_FOREVER)`, behind `L_Task_SB` when it waits, at the head otherwise |
| Measured operation | not a task | both | `k_sem_give()` on `test_sem` (`k_sem_init(&test_sem, 0U, K_SEM_MAX_LIMIT)`); `S_A` also times the tail of `k_sem_take()` on the same object |

## Measurement

In `S_A` the give finds `H_Task_SA` at the head of the wait queue, and `H_Task_SA > L_Task_SA`
switches. In `S_B` the give finds at the head a task `H_Task_SB` outranks, or an empty wait
queue, and `H_Task_SB` keeps the CPU.

| Scenario | Measurement window | What the window holds |
| --- | --- | --- |
| `S_A` | start marker in `L_Task_SA`, stop marker in `H_Task_SA` | the unpend of `H_Task_SA`, the switch, the tail of the `k_sem_take()` of `H_Task_SA` |
| `S_B`, head present | both markers in `H_Task_SB` | the unpend of the head, the tail of `k_sem_give()` |
| `S_B`, wait queue empty | both markers in `H_Task_SB` | the `sem->count` increment, the tail of `k_sem_give()` |

The two paths part where the give tests for a waiter: with one present they continue through
its unpend and the ready-queue insert, with none they take only the count increment.
`S_A - S_B` therefore holds the unpend and insert of the woken task, less the count increment
`S_B` takes when its wait queue is empty.

| Test case | `TEST_SB_HAS_WAITER` | `ADD_DUMMY_TASKS` | Measures |
| --- | --- | --- | --- |
| `TEST_CASE_I` | 1 | 0 | base measurement |
| `TEST_CASE_II` | 0 | 0 | base measurement |
| `TEST_CASE_III` | 1 | 1 | loaded measurement of `TEST_CASE_I` |
| `TEST_CASE_IV` | 0 | 1 | loaded measurement of `TEST_CASE_II` |

The sweep parameter is `DUMMY_TASKS_COUNT`, swept one build per value over `0..DUMMY_POOL_SIZE`
in the `TEST_CASE_III` and `TEST_CASE_IV` branches. No script runs the sweep: before each
build, set `#define DUMMY_TASKS_COUNT` in the branch of the built test case in
`benchmark_testcases.h` by hand. At `DUMMY_TASKS_COUNT` = 0, `TEST_CASE_III`
creates the same tasks as `TEST_CASE_I`, and `TEST_CASE_IV` the same as `TEST_CASE_II`. In
`TEST_CASE_IV`, `DUMMY_TASKS_COUNT` > 0 also moves its `S_B` path onto the one `TEST_CASE_I`
measures: the give takes `sb_dummy_threads[0]` at the head instead of finding the wait queue
empty.

`L_Task_SA`'s first iteration cycle runs when `I_Task` creates it, so `I_Task` resumes it
`MEASUREMENT_COUNT - 1` times. `I_Task` resets the semaphore before creating the `S_B` tasks,
so `S_B` starts with an empty wait queue and a count of 0. `H_Task_SB` suspends itself before
each iteration cycle, so `I_Task` resumes it `MEASUREMENT_COUNT` times.

## Compensation

None.

## Configuration

General configuration: [../README.md](../README.md#configuration).

| Option | Meaning |
| --- | --- |
| `CONFIG_POLL` | not set, Kconfig default n: the give with an empty wait queue does not reschedule |
