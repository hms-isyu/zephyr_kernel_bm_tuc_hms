# sem_preempt — preempting switch via `k_sem_give()`

Sources: [time_task_switch_sem.c](time_task_switch_sem.c),
[benchmark_testcases.h](benchmark_testcases.h)
Linked when `CONFIG_BENCHMARK_TEST_SEM_TASK_SWITCH_PREEMPT=y` (Kconfig choice
`BENCHMARK_TEST`).

## General description

Same shape as [../event_preempt](../event_preempt) — a higher priority thread is pending on a
synchronisation object and a lower priority thread releases it — but the object is a
semaphore. The difference in the kernel path is the wait-queue handling: `k_sem_give()` calls
`z_unpend_first_thread()` and releases the **head of the queue only**, where
`k_event_post()` walks the whole queue and wakes every matching waiter.

The scaffolding around the measured call (start barrier, per-round hand-off to `I_Task`) stays
on `k_event`, exactly as in the other task_switch tests, so that only the measured primitive
differs between this test and `event_preempt`.

| Series | Window | Measured chain |
| --- | --- | --- |
| `scenario_a` (S_A) | start marker in `L_Task_SA`, stop marker in `H_Task_SA` (cross-thread read window) | `k_sem_give()` entry, `z_unpend_first_thread()`, `z_ready_thread()`, `z_reschedule()`, the swap, and the tail of `k_sem_take()` in `H_Task_SA` |
| `scenario_b` (S_B) | start and stop marker inside `H_Task_SB` (same-function read window) | the same give where the head waiter cannot preempt the giver, or where there is no waiter at all and the give takes the `sem->count++` arm without reaching the scheduler |

`S_A - S_B` leaves the swap and the take tail when S_B has a waiter
(`TEST_SB_HAS_WAITER == 1`), and additionally the unpend and the ready-add when it has none
(`TEST_SB_HAS_WAITER == 0` with no dummy load).

The two series run one after the other: `I_Task` drives S_A for `MEASUREMENT_COUNT`
iterations, aborts the S_A threads, calls `k_sem_reset()` so that S_B starts from an empty
queue and a zero count, then creates the S_B set and drives it the same way.

## Kernel configuration behind the queues

| Option (`prj.conf`) | Value | Consequence for the queues |
| --- | --- | --- |
| `CONFIG_MP_MAX_NUM_CPUS` | 1 | one ready queue; exactly one thread runs |
| `CONFIG_SCHED_MULTIQ` | y | ready queue = one FIFO list per priority plus a bitmask (`z_priq_mq_*`); add, remove and "pick best" are O(1) and independent of occupancy |
| `CONFIG_WAITQ_SIMPLE` | y | `test_sem` and `test_event` each own one priority-sorted list (`z_priq_simple_*`); a pend walks it from the head, a give pops the head |
| `CONFIG_TIMESLICING` | n | no involuntary rotation inside a band |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | priorities 0..15, all preemptible; the kernel idle thread sits at `K_IDLE_PRIO` = 16, below every test thread |

Lower number = higher priority. *Band* = the ready-queue list of one priority. A thread that
calls `k_thread_suspend()` leaves the ready queue and joins no wait queue.

## Threads

| Thread | Priority | Phase | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | consumes `SIGNALIZE_YIELD_EVENT_MASK` and resumes the driver thread once per round; tears the phases down |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 2 | S_A | pends in `k_sem_take(K_FOREVER)`; takes the stop marker when the take returns |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | S_A | takes the start marker, gives the semaphore, is preempted, then self-suspends |
| `sa_dummy_threads[0..n-1]` (`dummy_task`) | `TASK_SA_DUMMY_1..4_PRIO` = 3, 4, 5, 6 | S_A | wait-queue load *behind* `H_Task_SA`: each pends in `k_sem_take(K_FOREVER)` |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | S_B | gives the semaphore and takes both markers; self-suspends at the end of each round |
| `L_Task_SB` | `TASK_SB_LOW_PRIO` = 10 | S_B | with `TEST_SB_HAS_WAITER == 1` pends in `k_sem_take(K_FOREVER)`; otherwise self-suspends and is resumed explicitly |
| `sb_dummy_threads[0..n-1]` (`dummy_task`) | `TASK_SB_DUMMY_1..4_PRIO` = 11, 12, 13, 14 | S_B | wait-queue load *behind* `L_Task_SB` |
| `sb_5_thread` (`dummy_task_b`) | `TASK_SB_DUMMY_5_PRIO` = 9 | S_B | created on `sa_dummy_stacks[0]` and suspends itself immediately; holds no queue slot |
| kernel idle | 16 | both | runs only when no test thread is ready |

## Queue snapshots at the start marker

One row per series per test case, read as
*releasing task [ … ] — ready queue [ … ] — wait queue [ … ]*:

- **Releasing task** — the thread that holds the CPU, takes the start marker and calls
  `k_sem_give()`.
- **Ready queue** — the other ready threads, highest priority first, `(priority)` after each
  name.
- **Wait queue** — the threads pending on `test_sem`, in queue order (priority-sorted); the
  first entry is the head that the give pops.
- **Effect** — what the give does to the queues, and where the stop marker is taken.

The kernel idle thread (`K_IDLE_PRIO` = 16) is ready in every row and is left out.
`SA_D1`…`SA_D4` are `sa_dummy_threads[]` (3, 4, 5, 6) and `SB_D1`…`SB_D4` are
`sb_dummy_threads[]` (11, 12, 13, 14), each created in pool order.

| Case | Series | Releasing task | Ready queue | Wait queue (`test_sem`) | Effect |
| --- | --- | --- | --- | --- | --- |
| I | S_A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2) | pops the head `H_Task_SA` into band 2 → swap → stop marker in `H_Task_SA` |
| I | S_B | `H_Task_SB` (8) | `I_Task` (15) | `L_Task_SB` (10) | pops the head `L_Task_SB` into band 10, below the giver → no swap → stop marker in `H_Task_SB` |
| II | S_A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2) | as case I |
| II | S_B | `H_Task_SB` (8) | `L_Task_SB` (10), `I_Task` (15) | — empty | no waiter at all: `sem->count` is incremented, no unpend, no ready-add, the scheduler is never reached → stop marker in `H_Task_SB` |
| III | S_A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2), `SA_D1` (3), `SA_D2` (4), `SA_D3` (5), `SA_D4` (6) | pops the head `H_Task_SA` only — the four dummies behind it are never visited → swap to band 2 → stop marker in `H_Task_SA` |
| III | S_B | `H_Task_SB` (8) | `I_Task` (15) | `L_Task_SB` (10), `SB_D1` (11), `SB_D2` (12), `SB_D3` (13), `SB_D4` (14) | pops the head `L_Task_SB` only → no swap → stop marker in `H_Task_SB` |
| IV | S_A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2), `SA_D1` (3), `SA_D2` (4), `SA_D3` (5), `SA_D4` (6) | as case III |
| IV | S_B | `H_Task_SB` (8) | `L_Task_SB` (10), `I_Task` (15) | `SB_D1` (11), `SB_D2` (12), `SB_D3` (13), `SB_D4` (14) | pops the head dummy `SB_D1` into band 11, below the giver → no swap → stop marker in `H_Task_SB`; the popped dummy re-pends when it next runs |

Reading the rows:

- Exactly one thread is ever unpended and readied, whatever the queue length: the give walks
  nothing. This is the difference to [../event_preempt](../event_preempt), where the post
  walks the whole queue and wakes every matching waiter.
- The S_B dummies sit at 11..14, *below* `L_Task_SB` at 10, so that the waiter which keeps the
  round chain alive is always the head that the give pops. The event variant can afford to put
  its `L_Task_SB` at the bottom of the queue, because `k_event_post()` wakes every matching
  waiter.
- The dummy load adds no walk to the re-pend either: `z_priq_simple_add()` stops at the first
  node the inserted thread outranks, which for `H_Task_SA` (2) and `L_Task_SB` (10) is the
  first node in the queue.
- With `TEST_SB_HAS_WAITER == 0` (cases II and IV) `L_Task_SB` does not pend; `H_Task_SB`
  resumes it before opening the window, which is why it appears in the ready queue there.
- `sb_5_thread` (9) suspends itself as soon as it runs and is never resumed: in every S_B row
  it holds neither a ready-queue slot nor a wait-queue node.

Two objects are in play, with the measured hand-off and the scaffolding deliberately kept
apart:

| Object | Use | In the measured path? |
| --- | --- | --- |
| `test_sem` (initial count 0, limit `K_SEM_MAX_LIMIT`) | the measured `k_sem_give()` / `k_sem_take()` pair | yes |
| `test_event`, `START_EVENT_MASK` (0x0008) | start barrier | no — before the first iteration |
| `test_event`, `SIGNALIZE_YIELD_EVENT_MASK` (0x0004) | driver thread → `I_Task`, one post per round | no — posted after the window closes |

## Test cases

Selected by `TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h). The priorities are
the same in all four cases (S_A 7 / 2, S_B 8 / 10).

| Case | `TEST_SB_HAS_WAITER` | `DUMMY_TASKS_COUNT` | What it isolates |
| --- | --- | --- | --- |
| `TEST_CASE_I` | 1 | 0 | switch + take tail, against a give that pops one waiter and does not switch |
| `TEST_CASE_II` | 0 | 0 | switch + take tail + unpend + ready-add, against a give that only increments the count |
| `TEST_CASE_III` | 1 | 4 | the same as I with four extra waiters queued behind the head on both sides |
| `TEST_CASE_IV` | 0 | 4 | a give that pops a non-preempting dummy head, against the switching give |

## Measurement bookkeeping

- `MEASUREMENT_COUNT` = 50000 iterations per series.
- `BMTH_mwindow_open()` / `BMTH_mwindow_close()` gate `BMTH_mseries_iterate()`: an iteration
  is only recorded while the window is open.
- `scenario_a` is initialised as a cross-function read window, `scenario_b` as an
  inside-function one, matching where their markers are taken.
- No `zephyr_sim.c` in this test: there are no simulated half-chains to subtract.
- `I_Task` reports through `BMTH_signalize_mseries_stop()`, failing if either series counted
  an outlier.
