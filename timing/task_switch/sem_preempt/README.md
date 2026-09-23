# sem_preempt, preempting switch via `k_sem_give()`

Link this test by setting `CONFIG_BENCHMARK_TEST_SEM_TASK_SWITCH_PREEMPT=y` in `prj.conf`
(Kconfig choice `BENCHMARK_TEST`, one test case per build) and rebuilding. Terminology:
[../README.md](../README.md).

## General

Same shape as [../event_preempt](../event_preempt): `H` is pending on a synchronisation object
and `L` releases it. The object is a semaphore. The difference in the kernel path is the
wait-queue handling, `k_sem_give()` calls `z_unpend_first_thread()` and releases the **head of
the queue only**, where `k_event_post()` walks the whole queue and wakes every matching waiter.

| Scenario | Trigger | Measurement window | Measured path |
| --- | --- | --- | --- |
| A | Transition | start marker in `L_Task_SA`, stop marker in `H_Task_SA` (cross-thread) | `k_sem_give()` entry, `z_unpend_first_thread()`, `z_ready_thread()`, `z_reschedule()`, the swap, and the tail of `k_sem_take()` in `H_Task_SA` |
| B | Activation API | start and stop marker inside `H_Task_SB` (same function) | the same give where the head waiter cannot preempt the giver, or where there is no waiter at all and the give takes the `sem->count++` arm without reaching the scheduler |

`S_A - S_B` leaves the swap and the take tail when S_B has a waiter
(`TEST_SB_HAS_WAITER == 1`), and additionally the unpend and the ready-add when it has none
(`TEST_SB_HAS_WAITER == 0` with no dummy load).

The scaffolding around the measured call (start barrier, per-round hand-off to `I_Task`) stays
on `k_event`, exactly as in the other task_switch tests, so that only the measured primitive
differs between this test and [../event_preempt](../event_preempt).

## Threads

| Thread | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | consumes `SIGNALIZE_YIELD_EVENT_MASK` and resumes the driver task once per round, tears the scenarios down |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 2 | A | pends in `k_sem_take(K_FOREVER)`, takes the stop marker when the take returns |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | A | takes the start marker, gives the semaphore, is preempted, then self-suspends |
| `sa_dummy_threads[0..n-1]` (`dummy_task`) | `TASK_SA_DUMMY_1..4_PRIO`, one band apart, `H > D > L` | A | wait-queue load, each pends in `k_sem_take(K_FOREVER)` |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | B | gives the semaphore and takes both markers, self-suspends at the end of each round |
| `L_Task_SB` | `TASK_SB_LOW_PRIO` = 10 | B | with `TEST_SB_HAS_WAITER == 1` pends in `k_sem_take(K_FOREVER)`, otherwise self-suspends and is resumed explicitly |
| `sb_dummy_threads[0..n-1]` (`dummy_task`) | `TASK_SB_DUMMY_1..4_PRIO`, one band apart, `L_Task_SB > D` | B | wait-queue load |
| `sb_5_thread` (`dummy_task_b`) | `TASK_SB_DUMMY_5_PRIO`, `H_Task_SB > sb_5_thread > L_Task_SB` | B | created on `sa_dummy_stacks[0]` and suspends itself immediately, holds no queue slot |
| kernel idle | 16 | both | runs only when no test task is ready |

On the S_A side `H > D > L`, so the dummies are enqueued behind the measured waiter. On the
S_B side `L_Task_SB > D`, so the waiter that keeps the round chain alive is always the head
the give pops.

## Measurement

Selected by `TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h). The priorities are
the same in all four cases (S_A 7 / 2, S_B 8 / 10).

| Case | `TEST_SB_HAS_WAITER` | `DUMMY_TASKS_COUNT` | Measures |
| --- | --- | --- | --- |
| `TEST_CASE_I` | 1 | 0 | base measurement: switch and take tail, against a give that pops one waiter and does not switch |
| `TEST_CASE_II` | 0 | 0 | base measurement, wider difference: switch, take tail, unpend and ready-add, against a give that only increments the count |
| `TEST_CASE_III` | 1 | `n` | loaded measurement of case I, `n` extra waiters queued behind the head on both sides |
| `TEST_CASE_IV` | 0 | `n` | loaded measurement of case II, a give that pops a non-preempting dummy head against the switching give |

`DUMMY_TASKS_COUNT` is the sweep parameter of the loaded measurement and is changed between
runs, one build per value, `n` = 1..4 (capped by `DUMMY_POOL_SIZE`). The value committed in
[benchmark_testcases.h](benchmark_testcases.h) is only the point that was measured last.

The scenarios run one after the other: `I_Task` drives S_A for `MEASUREMENT_COUNT` iterations,
aborts the S_A tasks, calls `k_sem_reset()` so that S_B starts from an empty queue and a zero
count, then creates the S_B set and drives it the same way.

## Compensation

TBD.

## Analysis

Documents: none in this directory yet.

Queue state at the start marker, the kernel idle task (16) is ready in every row and left out.
The wait queue is `test_sem` in queue order (priority sorted), the first entry is the head the
give pops. `SA_D1`…`SA_Dn` are `sa_dummy_threads[]` (3, 4, 5, 6) and `SB_D1`…`SB_Dn` are
`sb_dummy_threads[]` (11, 12, 13, 14), each created in pool order. Cases III and IV are shown
at `n` = 4, the top of the sweep.

| Case | Scenario | Releasing task | Ready queue | Wait queue (`test_sem`) | After the measured call |
| --- | --- | --- | --- | --- | --- |
| I | A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2) | pops the head `H_Task_SA` into band 2, swap, stop marker in `H_Task_SA` |
| I | B | `H_Task_SB` (8) | `I_Task` (15) | `L_Task_SB` (10) | pops the head `L_Task_SB` into band 10, giver > woken task, no swap, stop marker in `H_Task_SB` |
| II | A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2) | as case I |
| II | B | `H_Task_SB` (8) | `L_Task_SB` (10), `I_Task` (15) | empty | no waiter at all: `sem->count` is incremented, no unpend, no ready-add, the scheduler is never reached, stop marker in `H_Task_SB` |
| III | A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2), `SA_D1` (3), `SA_D2` (4), `SA_D3` (5), `SA_D4` (6) | pops the head `H_Task_SA` only, the `n` dummies behind it are never visited, swap to band 2, stop marker in `H_Task_SA` |
| III | B | `H_Task_SB` (8) | `I_Task` (15) | `L_Task_SB` (10), `SB_D1` (11), `SB_D2` (12), `SB_D3` (13), `SB_D4` (14) | pops the head `L_Task_SB` only, no swap, stop marker in `H_Task_SB` |
| IV | A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2), `SA_D1` (3), `SA_D2` (4), `SA_D3` (5), `SA_D4` (6) | as case III |
| IV | B | `H_Task_SB` (8) | `L_Task_SB` (10), `I_Task` (15) | `SB_D1` (11), `SB_D2` (12), `SB_D3` (13), `SB_D4` (14) | pops the head dummy `SB_D1` into band 11, giver > woken task, no swap, stop marker in `H_Task_SB`, the popped dummy re-pends when it next runs |

How the dummies load the workload:

- The load lands in the **wait queue** of `test_sem`, behind the head. The **ready queue**
  before the measured call holds only `I_Task` (and `L_Task_SB` where it was resumed instead
  of pended). After the call exactly one task has moved from the wait queue into its band,
  whatever the queue length: the give walks nothing. This is the difference to
  [../event_preempt](../event_preempt), where the post walks the whole queue and wakes every
  matching waiter.
- The dummy load adds no walk to the re-pend either. `z_priq_simple_add()` stops at the first
  node the inserted task does not outrank, which for `H_Task_SA` (2) and `L_Task_SB` (10) is
  the first node in the queue.
- With `TEST_SB_HAS_WAITER == 0` (cases II and IV) `L_Task_SB` does not pend, `H_Task_SB`
  resumes it before opening the window, which is why it appears in the ready queue there.
- `sb_5_thread` (9) suspends itself as soon as it runs and is never resumed, in every S_B row
  it holds neither a ready-queue slot nor a wait-queue node.

Two objects are in play, the measured hand-off and the scaffolding deliberately kept apart:

| Object | Use | In the measurement window |
| --- | --- | --- |
| `test_sem` (initial count 0, limit `K_SEM_MAX_LIMIT`) | the measured `k_sem_give()` / `k_sem_take()` pair | yes |
| `test_event`, `START_EVENT_MASK` (0x0008) | start barrier | no, before the first iteration |
| `test_event`, `SIGNALIZE_YIELD_EVENT_MASK` (0x0004) | driver task to `I_Task`, one post per round | no, posted after the window closes |

## Configuration

Shared configuration: [../README.md](../README.md).

| Option (`prj.conf`) | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_WAITQ_SIMPLE` | y | `test_sem` and `test_event` each own one priority-sorted list (`z_priq_simple_*`), a pend walks it from the head, a give pops the head |
