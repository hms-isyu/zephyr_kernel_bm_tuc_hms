# sem_preempt, preempting switch via `k_sem_give()`

Build: set `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_SEM_PREEMPT=y` in `prj.conf`, one test case
per build. `#define TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h) selects
`TEST_CASE_I` to `TEST_CASE_IV`. The point of the sweep is `#define DUMMY_TASKS_COUNT` in the
`TEST_CASE_III` and `TEST_CASE_IV` branches of that file. Terms, scenarios and shared
configuration: [../README.md](../README.md).

`k_sem_give()` hands the semaphore to the first task in its wait queue and makes it ready, or,
with the wait queue empty, increments the count. This test case times `k_sem_give()` by
`L_Task_SA` to `H_Task_SA` waiting in `k_sem_take()`, `H_Task_SA > L_Task_SA`, against the
give by `H_Task_SB`, which finds either a waiter it outranks or no waiter. The give takes the
head of the wait queue only (`z_unpend_first_thread()`), so dummy tasks waiting behind the head
are not visited. In [../event_preempt](../event_preempt), `k_event_post()` visits every waiter
of its wait queue through `z_sched_waitq_walk()`.

## Participants

| Name | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | creates `H_Task_SA`, `sa_dummy_threads[0..dummy_count-1]` and `L_Task_SA`, resumes `L_Task_SA` once per round after `SIGNALIZE_YIELD_EVENT_MASK`, aborts the `S_A` tasks, calls `k_sem_reset()`, creates `H_Task_SB`, `L_Task_SB` and `sb_dummy_threads[0..dummy_count-1]`, resumes `H_Task_SB` once per round |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 2 | `S_A` | waits in `k_sem_take(K_FOREVER)`, takes the stop marker when it returns, posts `SIGNALIZE_YIELD_EVENT_MASK`, calls `k_sem_take()` again |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | `S_A` | opens the window, takes the start marker, calls `k_sem_give()`, suspends itself |
| `sa_dummy_threads[i]` (`dummy_task`) | `H_Task_SA > D > L_Task_SA` | `S_A` | wait in `k_sem_take(K_FOREVER)` behind `H_Task_SA` and are never woken |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | `S_B` | suspends itself. Resumed, it opens the window, takes both markers around `k_sem_give()` and closes the window. With `TEST_SB_HAS_WAITER == 0` it then resumes `L_Task_SB`, outside the window. |
| `L_Task_SB` | `TASK_SB_LOW_PRIO` = 10 | `S_B` | per round posts `SIGNALIZE_YIELD_EVENT_MASK`, then, with `TEST_SB_HAS_WAITER == 1`, calls `k_sem_take(K_FOREVER)` and waits at the head of the wait queue, with `TEST_SB_HAS_WAITER == 0` suspends itself |
| `sb_dummy_threads[i]` (`dummy_task`) | `H_Task_SB > L_Task_SB > D` | `S_B` | wait in `k_sem_take(K_FOREVER)`. A dummy task the give wakes calls `k_sem_take()` again in the loop of `dummy_task` before `I_Task` runs, so the wait queue holds `dummy_count` dummy tasks in every round. |

Every `TEST_CASE` branch defines the same priorities: `TASK_SA_HIGH_PRIO` = 2,
`TASK_SA_LOW_PRIO` = 7, `TASK_SB_HIGH_PRIO` = 8, `TASK_SB_LOW_PRIO` = 10.

The measured operation is `k_sem_give()` on `test_sem` (`k_sem_init(&test_sem, 0U,
K_SEM_MAX_LIMIT)`), and in `S_A` the tail of `k_sem_take()` on the same object.

## Measurement

In `S_A` the give finds `H_Task_SA` at the head of the wait queue, and `H_Task_SA > L_Task_SA`
switches. In `S_B` the give finds at the head a task `H_Task_SB` outranks, or an empty wait
queue, and `H_Task_SB` keeps the CPU.

| Scenario | Trigger | Measurement window | What the window holds |
| --- | --- | --- | --- |
| `S_A` | `L_Task_SA` calls `k_sem_give()` | start marker in `L_Task_SA`, stop marker in `H_Task_SA` | `z_unpend_first_thread()` taking `H_Task_SA`, `z_ready_thread()`, the arm of `reschedule()` that calls `z_swap()`, the PendSV exception restoring `H_Task_SA`, the tail of the `k_sem_take()` of `H_Task_SA` |
| `S_B`, head present | `H_Task_SB` calls `k_sem_give()` with `L_Task_SB` or `sb_dummy_threads[0]` at the head | both markers in `H_Task_SB` | `z_unpend_first_thread()` taking the head, `z_ready_thread()`, the arm of `reschedule()` without `z_swap()`, the tail of `k_sem_give()` |
| `S_B`, wait queue empty | `H_Task_SB` calls `k_sem_give()` with no waiter | both markers in `H_Task_SB` | `z_unpend_first_thread()` returning `NULL`, the `sem->count` increment, `k_spin_unlock()`, the tail of `k_sem_give()`. No `z_ready_thread()`, no `z_reschedule()` (`handle_poll_events()` returns false). |

With a head present in `S_B`, the paths part at the `need_swap()` test in `reschedule()`. With
the wait queue empty, they part at the `thread != NULL` test in `z_impl_k_sem_give()`, and
`S_A - S_B` also holds the unpend of a waiter, `z_ready_thread()` and `z_reschedule()` up to
its `need_swap()` test, less the count increment. At `CONFIG_SCHED_MULTIQ` = n the ready queue
insert of the woken task passes no task in `S_A` and `H_Task_SB` in `S_B`, one walk step less
in `S_A`.

| Test case | `TEST_SB_HAS_WAITER` | `ADD_DUMMY_TASKS` | `S_B` path | Measures |
| --- | --- | --- | --- | --- |
| `TEST_CASE_I` | 1 | 0 | head present, `L_Task_SB` | base measurement |
| `TEST_CASE_II` | 0 | 0 | wait queue empty | base measurement |
| `TEST_CASE_III` | 1 | 1 | head present, `L_Task_SB`, `dummy_count` dummy tasks behind it | loaded measurement of `TEST_CASE_I` |
| `TEST_CASE_IV` | 0 | 1 | head present, `sb_dummy_threads[0]` at `dummy_count` > 0, wait queue empty at 0 | loaded measurement of `TEST_CASE_II` |

In `S_A` every loaded test case queues `dummy_count` dummy tasks behind `H_Task_SA`. In
`TEST_CASE_IV` the dummy tasks change the `S_B` path itself: at `dummy_count` > 0 the give takes
`sb_dummy_threads[0]`, the path of `TEST_CASE_I`, instead of the count increment of
`TEST_CASE_II`.

The sweep parameter is `DUMMY_TASKS_COUNT`, swept one build per value over `0..DUMMY_POOL_SIZE`
in the `TEST_CASE_III` and `TEST_CASE_IV` branches. At `DUMMY_TASKS_COUNT` = 0, `TEST_CASE_III`
creates the same tasks as `TEST_CASE_I`, and `TEST_CASE_IV` the same as `TEST_CASE_II`.

`S_A` runs first. Its first round runs when `L_Task_SA` is created, so `I_Task` resumes
`L_Task_SA` `MEASUREMENT_COUNT - 1` times. `I_Task` then aborts the `S_A` tasks and calls
`k_sem_reset()`, so `S_B` starts with an empty wait queue and a count of 0. `H_Task_SB`
suspends itself before each round, so `I_Task` resumes it `MEASUREMENT_COUNT` times.

## Compensation

No probe: the directory holds no `zephyr_sim.c`.

## Analysis

Expected of `TEST_CASE_I`: `S_A - S_B` is the `z_swap()` prologue, the PendSV exception and
the tail of `k_sem_take()`, less the unlock and the tail of `k_sem_give()` on the arm without
`z_swap()`.

Expected of `TEST_CASE_II`: `S_A - S_B` exceeds that of `TEST_CASE_I` by the unpend of a
waiter, `z_ready_thread()` and `z_reschedule()` up to its `need_swap()` test, which the `S_B`
of `TEST_CASE_I` holds and the `S_B` of `TEST_CASE_II` does not, less the count increment.

Expected of the loaded test cases: the give takes the head in `O(1)` (`z_priq_simple_best()`),
and no dummy task is ready inside a window, so neither series is expected to carry a term in
`DUMMY_TASKS_COUNT` at either value of `CONFIG_SCHED_MULTIQ`. At `DUMMY_TASKS_COUNT` > 0,
`TEST_CASE_III` and `TEST_CASE_IV` are expected to show the `S_A - S_B` of `TEST_CASE_I`.

## Configuration

| Option | Value | Consequence special to this test case |
| --- | --- | --- |
| `CONFIG_WAITQ_SIMPLE` | y, Kconfig default | `k_sem_give()` takes the head in `O(1)`, the dummy tasks behind it are not visited |
| `CONFIG_POLL` | n, resolved | `handle_poll_events()` returns false, so the give with an empty wait queue reaches no `z_reschedule()` |
