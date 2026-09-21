# task_preempt — preempting switch via `k_thread_resume()`

Sources: [time_task_switch.c](time_task_switch.c), [zephyr_sim.c](zephyr_sim.c),
[benchmark_testcases.h](benchmark_testcases.h)
Linked when `CONFIG_BENCHMARK_TEST_TASK_SWITCH_PREEMPT=y` (Kconfig choice `BENCHMARK_TEST`).

## General description

The switch is triggered by readying a suspended thread. A thread that suspends itself leaves
the ready queue without joining any wait queue, so this test exercises the ready queue and
the swap chain alone — no wait-queue walk, no synchronisation object in the measured path.

| Series | Window | Measured chain |
| --- | --- | --- |
| `scenario_a` (S_A) | start marker in `L_Task_SA`, stop marker in `H_Task_SA` (cross-thread read window) | `k_thread_resume()` entry, `z_ready_thread()`, `reschedule()`, the swap, and the return half of `H_Task_SA`'s own `k_thread_suspend()` |
| `scenario_b` (S_B) | start and stop marker inside `Task_SB` (same-function read window) | the same `k_thread_resume()` on a thread of *lower* priority than the caller: entry, `z_ready_thread()`, `reschedule()` taking the no-swap arm, and the return of `k_thread_resume()` |

Everything up to `reschedule()`'s swap decision is common to both series and cancels in
`S_A - S_B`; what is left is the swap chain plus the tail of the suspend call in which S_A
stops. Both leftover halves are measured separately by the probes in
[zephyr_sim.c](zephyr_sim.c) (see below), because neither can be timed in the kernel itself:
the window would have to open inside the context switch.

The series run one after the other. `I_Task` creates the S_A pair; when they have suspended
themselves it aborts `L_Task_SA`, creates the dummy load and `Task_SB`.

## Kernel configuration behind the queues

| Option (`prj.conf`) | Value | Consequence for the queues |
| --- | --- | --- |
| `CONFIG_MP_MAX_NUM_CPUS` | 1 | one ready queue; exactly one thread runs |
| `CONFIG_SCHED_MULTIQ` | y | ready queue = one FIFO list per priority plus a bitmask (`z_priq_mq_*`); add, remove and "pick best" are O(1) and independent of occupancy |
| `CONFIG_WAITQ_SIMPLE` | y | every `k_event` / `k_sem` owns one priority-sorted list (`z_priq_simple_*`); an insert walks from the head until it meets a thread the inserted one outranks |
| `CONFIG_TIMESLICING` | n | no involuntary rotation inside a band |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | priorities 0..15, all preemptible; the kernel idle thread sits at `K_IDLE_PRIO` = 16, below every test thread |

Lower number = higher priority. *Band* = the ready-queue list of one priority.

This is the test where the ready-queue implementation matters most: under `SCHED_MULTIQ` the
`z_ready_thread()` of the resumed thread is an O(1) append to its own band, so the dummy load
below should not change the measured value. Under `CONFIG_SCHED_SIMPLE` the ready queue is a
single sorted list and the insert walks every ready thread that outranks the resumed one, so
the same sweep loads the measured path directly.

## Threads

| Thread | Priority | Phase | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | creates and tears down the measured threads, then spins on the result signal |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 8 | S_A | suspends itself each round; takes the stop marker when the suspend call returns |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 9 | S_A | suspends `H_Task_SA`, takes the start marker, resumes it and is preempted |
| `Task_SB` | `TASK_SB_HIGH_PRIO` = 2 | S_B | suspends and resumes `H_Task_SA` from above; keeps the CPU |
| `sb_dummy_threads[0..n-1]` (`dummy_task_b`) | `TASK_SB_DUMMY_1..4_PRIO` = 3, 4, 5, 6 | S_B | ready-queue load between the caller's band and the resumed thread's band; each self-suspends when it runs |
| kernel idle | 16 | both | runs only when no test thread is ready |

`sa_dummy_threads` / `TASK_SA_DUMMY_*_PRIO` (3..6) are allocated and defined but never created
in this test: the dummy load exists on the S_B side only. `TASK_SB_LOW_PRIO` (2) is defined
but unused.

## Queue snapshots at the start marker

One row per series per test case, read as
*releasing task [ … ] — ready queue [ … ] — suspended [ … ] — wait queue [ … ]*:

- **Releasing task** — the thread that holds the CPU, takes the start marker and makes the
  measured call.
- **Ready queue** — the other ready threads, highest priority first, `(priority)` after each
  name.
- **Suspended** — threads in `_THREAD_SUSPENDED`: in no ready queue and in no wait queue.
- **Wait queue** — the threads pending on a synchronisation object, in queue order.
- **Effect** — what the measured call does to the queues, and where the stop marker is taken.

The kernel idle thread (`K_IDLE_PRIO` = 16) is ready in every row and is left out.
`SB_D1` (3), `SB_D2` (4), `SB_D3` (5), `SB_D4` (6) are `sb_dummy_threads[]`, created in pool
order; `n` = `DUMMY_TASKS_COUNT` (0..4).

| Case | Series | Releasing task | Ready queue | Suspended (in no queue) | Wait queue | Effect |
| --- | --- | --- | --- | --- | --- | --- |
| I | S_A | `L_Task_SA` (9) | `I_Task` (15) | `H_Task_SA` (8) | — empty | `k_thread_resume()` appends `H_Task_SA` to band 8, now the highest occupied band → swap → stop marker in `H_Task_SA`, as its own `k_thread_suspend()` returns |
| I | S_B | `Task_SB` (2) | `I_Task` (15) | `H_Task_SA` (8) | — empty | `k_thread_resume()` appends `H_Task_SA` to band 8, below the caller → no swap → stop marker in `Task_SB` |
| II | S_A | `L_Task_SA` (9) | `I_Task` (15) | `H_Task_SA` (8) | — empty | as case I — this test carries no dummy load on the S_A side |
| II | S_B | `Task_SB` (2) | `SB_D1` (3) … `SB_Dn`, `I_Task` (15) | `H_Task_SA` (8) | — empty | as case I, with `n` threads queued between the caller's band 2 and band 8 |

`H_Task_SA` is the resumed thread in both series; `Task_SB` re-suspends it at the top of every
S_B round, so it never actually runs while S_B is measuring.

The dummies are resumed *before* `BMTH_mwindow_open()` and suspended again *after*
`BMTH_mwindow_close()`, so their own resume/suspend cost stays outside the window — only
their presence in the ready queue is inside it.

No wait queue is involved in either measured chain. `test_event` carries only the start
barrier (`START_EVENT_MASK`, 0x0008), on which `H_Task_SA` and `L_Task_SA` pend once before
the first iteration; `SCENARIO_A_EVENT_MASK` and `SIGNALIZE_YIELD_EVENT_MASK` are defined but
never posted. Suspension is a thread state, not a queue: this is what makes the test a pure
ready-queue counterpart to the `k_event` and `k_sem` tests, where the same switch is driven
out of a wait queue.

## Test cases

Selected by `TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h).

| Case | S_A priorities (L / H) | S_B caller | Dummy load | What it isolates |
| --- | --- | --- | --- | --- |
| `TEST_CASE_I` | 9 / 8 | 2 | none | the bare resume-driven switch against the bare no-swap resume |
| `TEST_CASE_II` | 9 / 8 | 2 | `DUMMY_TASKS_COUNT` threads at 3..6 (0..4, capped by `DUMMY_POOL_SIZE`), S_B side only | the ready-queue insert with threads queued between the caller and the resumed thread |

## Compensation probes

Measured before the series start, in `main()`, from [zephyr_sim.c](zephyr_sim.c):

| Probe | Series | What it re-runs without a switch |
| --- | --- | --- |
| `measure_thread_resume_tail()` | `z_swap_overhead` | the return chain of `k_thread_resume()` (the S_B tail) |
| `measure_thread_suspend_tail()` | `suspend_tail` | the half of `k_thread_suspend()` that runs after the switch — the part inside S_A's window, since S_A stops the moment the call returns |
| `measure_swap_prologue_overhead()` | `swap_prologue` | the swap chain from its entry to the `irq_unlock()` where PendSV fires; the resume prologue ahead of it is shared with S_B and cancels out |

## Measurement bookkeeping

- `MEASUREMENT_COUNT` = 50000 iterations per series.
- `BMTH_mwindow_open()` / `BMTH_mwindow_close()` gate `BMTH_mseries_iterate()`: an iteration
  is only recorded while the window is open.
- `scenario_a` is initialised as a cross-function read window, `scenario_b` as an
  inside-function one, matching where their markers are taken.
- `I_Task` reports through `BMTH_signalize_mseries_stop()`, failing if either series counted
  an outlier.
