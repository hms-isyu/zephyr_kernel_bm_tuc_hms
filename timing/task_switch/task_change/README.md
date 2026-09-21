# task_change — same-priority switch via `k_yield()`

Sources: [time_task_switch.c](time_task_switch.c), [benchmark_testcases.h](benchmark_testcases.h)
Linked when `CONFIG_BENCHMARK_TEST_TASK_SWITCH_CHANGE=y` (Kconfig choice `BENCHMARK_TEST`).

> The Kconfig prompt for this choice reads *"Task switch via `k_thread_priority_set()`"*; the
> code triggers the switch with `k_yield()` between threads that already share a priority.

## General description

Both measured threads sit in the same priority band, so the scheduler has no preemption
decision to make: the switch happens only because the running thread hands the CPU on with
`k_yield()`, which rotates it to the tail of its own band and picks the new head.

| Series | Window | Measured chain |
| --- | --- | --- |
| `scenario_a` (S_A) | start marker in `L_Task_SA`, stop marker in `H_Task_SA` (cross-thread read window) | `k_yield()` entry, band rotation, `reschedule()`, the swap, and the return of `H_Task_SA`'s own `k_yield()` |
| `scenario_b` (S_B) | start and stop marker inside `Task_SB` (same-function read window) | `k_yield()` on a thread that is alone in the highest occupied band: rotation and `reschedule()` run, but the queue head does not change, so no swap happens |

`S_A - S_B` therefore isolates the context switch itself, with the yield API and the
scheduler decision cancelling out.

The two series run one after the other: `I_Task` creates the S_A threads, releases them with
`START_EVENT_MASK`, and only after they have suspended themselves does it abort them and
create `Task_SB`.

## Kernel configuration behind the queues

| Option (`prj.conf`) | Value | Consequence for the queues |
| --- | --- | --- |
| `CONFIG_MP_MAX_NUM_CPUS` | 1 | one ready queue; exactly one thread runs |
| `CONFIG_SCHED_MULTIQ` | y | ready queue = one FIFO list per priority plus a bitmask (`z_priq_mq_*`); add, remove and "pick best" are O(1) and independent of occupancy |
| `CONFIG_WAITQ_SIMPLE` | y | every `k_event` / `k_sem` owns one priority-sorted list (`z_priq_simple_*`); an insert walks from the head until it meets a thread the inserted one outranks |
| `CONFIG_TIMESLICING` | n | no involuntary rotation inside a band; equal-priority threads change only on an explicit call |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | priorities 0..15, all preemptible; the kernel idle thread sits at `K_IDLE_PRIO` = 16, below every test thread |

Lower number = higher priority. *Band* = the ready-queue list of one priority. A thread that
calls `k_thread_suspend()` leaves the ready queue and joins no wait queue.

With `CONFIG_SCHED_SIMPLE` in place of `CONFIG_SCHED_MULTIQ` the ready queue is a single
sorted list, `k_yield()` re-inserts by walking forward from the yielding thread's position,
and the band occupancy swept here enters the measured path.

## Threads

| Thread | Priority | Phase | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | creates and tears down the measured threads, then spins on the result signal |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 3 | S_A | opens the window, takes the start marker, yields |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 3 | S_A | takes the stop marker when its `k_yield()` returns |
| `sa_dummy_threads[0..n-1]` | `TASK_SA_DUMMY_1..4_PRIO` = 3 | S_A | band load; also close the window and own the termination check when present |
| `Task_SB` | `TASK_SB_HIGH_PRIO` = 2 | S_B | yields with nothing else in its band |
| kernel idle | 16 | both | runs only when no test thread is ready |

`sb_dummy_threads` / `TASK_SB_DUMMY_*_PRIO` (10..13) are allocated and defined but never
created in this test. The `L_`/`H_` names are kept from the preempting tests; here both
threads are at priority 3.

## Queue snapshots at the start marker

One row per series per test case, read as
*releasing task [ … ] — ready queue [ … ] — wait queue [ … ]*:

- **Releasing task** — the thread that holds the CPU, takes the start marker and makes the
  measured call.
- **Ready queue** — the other ready threads, highest priority first, `(priority)` after each
  name.
- **Wait queue** — the threads pending on a synchronisation object, in queue order.
- **Effect** — what the measured call does to the queues, and where the stop marker is taken.

The kernel idle thread (`K_IDLE_PRIO` = 16) is ready in every row and is left out.
`SA_D1`…`SA_D4` are `sa_dummy_threads[]`, created in pool order; `n` = `DUMMY_TASKS_COUNT`
(0..4).

| Case | Series | Releasing task | Ready queue | Wait queue | Effect |
| --- | --- | --- | --- | --- | --- |
| I | S_A | `L_Task_SA` (3) | `H_Task_SA` (3), `I_Task` (15) | — empty | `k_yield()` rotates `L_Task_SA` to the tail of band 3, the head becomes `H_Task_SA` → swap → stop marker in `H_Task_SA` |
| I | S_B | `Task_SB` (2) | `I_Task` (15) | — empty | `k_yield()` rotates `Task_SB` inside band 2, which stays the head → no swap → stop marker in `Task_SB` |
| II | S_A | `L_Task_SA` (3) | `H_Task_SA` (3), `SA_D1` (3) … `SA_Dn` (3), `I_Task` (15) | — empty | as case I, with `n` extra threads queued in band 3 behind `H_Task_SA` |
| II | S_B | `Task_SB` (2) | `I_Task` (15) | — empty | as case I — the dummy load exists on the S_A side only |

Band 3 holds every S_A thread, in the order `L_Task_SA`, `H_Task_SA`, `SA_D1` … `SA_Dn`. It is
filled by the `START_EVENT_MASK` post, which walks the event wait queue in priority order and,
at equal priority, in insertion order, and readies each waiter in that order.

One rotation of band 3 is one round:
`L_Task_SA → H_Task_SA → SA_D1 → … → SA_Dn → L_Task_SA`. Only the `L_Task_SA → H_Task_SA`
hand-off carries the markers; the dummies are band occupancy, which under `SCHED_MULTIQ`
should leave the switch cost unchanged.

No wait queue is involved in either measured chain. `test_event` is used only outside the
measurement: `START_EVENT_MASK` (0x0008) as the start barrier, on which the whole S_A set pends
once before the first iteration; `SCENARIO_A_EVENT_MASK` (0x0001) and
`SIGNALIZE_YIELD_EVENT_MASK` (0x0004) are defined but never posted. Once the barrier is
released the event wait queue stays empty for the rest of the run.

## Test cases

Selected by `TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h).

| Case | S_A / S_B priorities | Dummy load | What it isolates |
| --- | --- | --- | --- |
| `TEST_CASE_I` | 3 / 3, S_B at 2 | none | the bare `L → H` yield switch, two threads in the band |
| `TEST_CASE_II` | 3 / 3, S_B at 2 | `DUMMY_TASKS_COUNT` threads at priority 3 (0..4, capped by `DUMMY_POOL_SIZE`) | the same switch with the band loaded — the sweep parameter |

With the dummy load on, `H_Task_SA` skips its `BMTH_mwindow_close()` and termination check
(`if (DUMMY_TASKS_COUNT == 0U)`) and `dummy_task` takes both over, so that the closing thread
is always the one that ran last before `L_Task_SA` re-opens the window.

## Measurement bookkeeping

- `MEASUREMENT_COUNT` = 50000 iterations per series.
- `BMTH_mwindow_open()` / `BMTH_mwindow_close()` gate `BMTH_mseries_iterate()`: an iteration
  is only recorded while the window is open, so any thread that runs inside the window can
  invalidate it by closing it.
- `scenario_a` is initialised as a cross-function read window, `scenario_b` as an
  inside-function one, matching where their markers are taken.
- Both series end by suspending their threads; `I_Task` then reports through
  `BMTH_signalize_mseries_stop()`, failing if either series counted an outlier.
