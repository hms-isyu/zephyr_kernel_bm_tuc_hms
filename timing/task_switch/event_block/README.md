# event_block — blocking switch via `k_event_wait_safe()`

Sources: [time_task_switch_event.c](time_task_switch_event.c), [zephyr_sim.c](zephyr_sim.c),
[sim_measure.h](sim_measure.h), [benchmark_testcases.h](benchmark_testcases.h)
Linked when `CONFIG_BENCHMARK_TEST_EVENT_TASK_SWITCH_BLOCK=y` (Kconfig choice
`BENCHMARK_TEST`).

## General description

The mirror image of [../event_preempt](../event_preempt): the switch is not caused by a post
that wakes somebody, but by the *waiter itself* giving up the CPU. `H_Task_SA` takes the start
marker and calls `k_event_wait_safe()` on a bit that is not set, so the call runs into
`z_pend_curr()`: the thread is inserted into the priority-sorted wait queue of the object and
the scheduler hands the CPU to the next ready thread, which takes the stop marker.

| Series | Window | Measured chain |
| --- | --- | --- |
| `scenario_a` (S_A) | start marker in `H_Task_SA`, stop marker in `L_Task_SA` (cross-thread read window) | `k_event_wait_safe()` entry, the condition test, `z_pend_curr()` with the priority-ordered insert into the wait queue, `reschedule()`, the swap, and the resumption of `L_Task_SA` |
| `scenario_b` (S_B) | start and stop marker inside `H_Task_SB` (same-function read window) | the same call with `K_NO_WAIT` on a bit that is never posted: entry, condition test, unlock, return — it never pends and never reaches the scheduler |

`S_A - S_B` therefore isolates the pend itself: the wait-queue insert, the reschedule, the
swap, and the entry into the next thread.

Because S_A stops in a *different* thread than it starts in, the value also contains the part
of `L_Task_SA` up to its counter read; the two probes in [zephyr_sim.c](zephyr_sim.c) account
for the halves that cannot be timed in the kernel itself.

The two series run one after the other: `I_Task` drives S_A for `MEASUREMENT_COUNT`
iterations, aborts the S_A threads, clears `SCENARIO_A_EVENT_MASK`, then creates `H_Task_SB`
and drives it the same way.

## Kernel configuration behind the queues

| Option (`prj.conf`) | Value | Consequence for the queues |
| --- | --- | --- |
| `CONFIG_MP_MAX_NUM_CPUS` | 1 | one ready queue; exactly one thread runs |
| `CONFIG_SCHED_MULTIQ` | y | ready queue = one FIFO list per priority plus a bitmask (`z_priq_mq_*`); add, remove and "pick best" are O(1) and independent of occupancy |
| `CONFIG_WAITQ_SIMPLE` | y | `test_event` owns one priority-sorted list (`z_priq_simple_*`); the pend measured here walks it from the head until it meets a thread the pending one outranks |
| `CONFIG_TIMESLICING` | n | no involuntary rotation inside a band |
| `CONFIG_EVENTS` | y | `k_event` available |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | priorities 0..15, all preemptible; the kernel idle thread sits at `K_IDLE_PRIO` = 16, below every test thread |

Lower number = higher priority. *Band* = the ready-queue list of one priority. A thread that
calls `k_thread_suspend()` leaves the ready queue and joins no wait queue.

## Threads

| Thread | Priority | Phase | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | consumes `SIGNALIZE_YIELD_EVENT_MASK` and resumes the driver thread once per round; tears the phases down |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 6 | S_A | opens the window, takes the start marker and blocks in `k_event_wait_safe()` — the measured thread |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | S_A | takes the stop marker when it gets the CPU, posts `SCENARIO_A_EVENT_MASK` to re-arm `H_Task_SA`, then self-suspends |
| `sa_dummy_threads[0..n-1]` (`dummy_task`) | `TASK_SA_DUMMY_1..4_PRIO` = 2, 3, 4, 5 | S_A | wait-queue load *above* `H_Task_SA`: each pends on `SCENARIO_A_EVENT_MASK`, so each sits ahead of `H_Task_SA` in the queue that `H_Task_SA` inserts itself into |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | S_B | takes both markers around a `K_NO_WAIT` wait; self-suspends at the end of each round |
| kernel idle | 16 | both | runs only when no test thread is ready |

`sb_dummy_threads`, `TASK_SB_DUMMY_*_PRIO` (10..13) and `TASK_SB_LOW_PRIO` (9) are allocated
and defined but unused: there is no S_B low-priority thread and no dummy load on the S_B side.

## Queue snapshots at the start marker

One row per series per test case, read as
*blocking task [ … ] — ready queue [ … ] — wait queue [ … ]*:

- **Blocking task** — the thread that holds the CPU, takes the start marker and calls
  `k_event_wait_safe()`. Here it is the *waiter* that is measured, not a releasing thread.
- **Ready queue** — the other ready threads, highest priority first, `(priority)` after each
  name.
- **Wait queue** — the threads already pending on `test_event`, in queue order
  (priority-sorted), i.e. the queue the blocking task inserts itself into.
- **Effect** — what the call does to the queues, and where the stop marker is taken.

The kernel idle thread (`K_IDLE_PRIO` = 16) is ready in every row and is left out.
`SA_D1` (2), `SA_D2` (3), `SA_D3` (4), `SA_D4` (5) are `sa_dummy_threads[]`, created in pool
order; `n` = `DUMMY_TASKS_COUNT` (0..4).

| Case | Series | Blocking task | Ready queue | Wait queue (`test_event`) | Effect |
| --- | --- | --- | --- | --- | --- |
| I | S_A | `H_Task_SA` (6) | `L_Task_SA` (7), `I_Task` (15) | — empty | the wait inserts `H_Task_SA` into the empty queue, band 6 empties, `_priq_run_best` returns `L_Task_SA` → swap → stop marker in `L_Task_SA` |
| I | S_B | `H_Task_SB` (8) | `I_Task` (15) | — empty | the `K_NO_WAIT` wait tests the bit and returns without inserting → stop marker in `H_Task_SB` |
| II | S_A | `H_Task_SA` (6) | `L_Task_SA` (7), `I_Task` (15) | `SA_D1` (2) … `SA_Dn` | the insert walks the `n` dummy nodes before placing `H_Task_SA` behind them, then as case I → stop marker in `L_Task_SA` |
| II | S_B | `H_Task_SB` (8) | `I_Task` (15) | — empty | as case I — the dummy load exists on the S_A side only |

Reading the rows:

- The dummy load changes the **wait queue**, not the ready queue: each dummy was woken by
  `L_Task_SA`'s post, closed the `scenario_a` window and re-pended before `H_Task_SA` got the
  CPU, so at the start marker all `n` of them are back in the queue and `H_Task_SA` has to
  walk past them.
- The dummy priorities are deliberately *above* `H_Task_SA`: `z_priq_simple_add()` only walks
  the entries the inserted thread does not outrank, so putting the load at 2..5 puts all of it
  in front of the priority-6 waiter and makes the insert cost scale with the load.
- S_B never inserts, so its wait queue stays empty; `I_Task` clears
  `SCENARIO_A_EVENT_MASK` and aborts the S_A threads before the phase starts.

`test_event` carries every signal in the test:

| Mask | Direction | In the measured path? |
| --- | --- | --- |
| `SCENARIO_A_EVENT_MASK` (0x0010) | the bit `H_Task_SA` and the S_A dummies wait on, posted by `L_Task_SA` | yes — the measured pend of S_A is the insert into this queue |
| `SCENARIO_B_EVENT_MASK` (0x0002) | never posted | yes — the `K_NO_WAIT` wait of S_B tests it and returns |
| `SIGNALIZE_YIELD_EVENT_MASK` (0x0004) | driver thread → `I_Task` | no — posted after the window closes |
| `START_EVENT_MASK` (0x0008) | start barrier | no — before the first iteration |

Round order in S_A: `I_Task` resumes `L_Task_SA` → `L_Task_SA` posts
`SCENARIO_A_EVENT_MASK` → the dummies (bands 2..5) run first, each closing the `scenario_a`
window and re-pending → `H_Task_SA` (band 6) runs, re-opens the window, takes the start marker
and blocks → `L_Task_SA` (band 7) takes the stop marker at the top of its loop, closes the
window, posts `SIGNALIZE_YIELD_EVENT_MASK` and self-suspends → `I_Task` runs again.

## Test cases

Selected by `TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h). The priorities are
the same in both populated cases (S_A 7 / 6, S_B 8).

| Case | Dummy load | What it isolates |
| --- | --- | --- |
| `TEST_CASE_I` | none | the bare pend: insert into an empty wait queue, reschedule, swap |
| `TEST_CASE_II` | `DUMMY_TASKS_COUNT` threads at 2..5 (0..4, capped by `DUMMY_POOL_SIZE`) | the same pend with the queue loaded ahead of the waiter |

`TEST_CASE_III`..`TEST_CASE_VI` are enumerated in the header but carry no definitions.

## Compensation probes

Measured before the series start, in `main()`, from [zephyr_sim.c](zephyr_sim.c). Every
simulated chain is cut at the `irq_unlock()` where the kernel pends PendSV and the thread
leaves the CPU (see [sim_measure.h](sim_measure.h)).

| Probe | Series | What it re-runs without a switch |
| --- | --- | --- |
| `measure_event_z_swap_overhead(SIM_Z_SWAP_PROLOGUE)` | `z_swap_overhead` | the swap chain from entry to the `irq_unlock()` where PendSV would fire |
| `measure_wait_tail_no_wait_overhead()` | `wait_tail_no_wait` | the tail of `k_event_wait_safe()` for a `K_NO_WAIT` wait, which returns without pending — the S_B counterpart |

## Measurement bookkeeping

- `MEASUREMENT_COUNT` = 50000 iterations per series.
- `BMTH_mwindow_open()` / `BMTH_mwindow_close()` gate `BMTH_mseries_iterate()`: an iteration
  is only recorded while the window is open. `dummy_task` closes the `scenario_a` window, so
  an iteration in which a dummy ran after `H_Task_SA` opened it is discarded.
- `scenario_a` is initialised as a cross-function read window, `scenario_b` as an
  inside-function one, matching where their markers are taken.
- `I_Task` reports through `BMTH_signalize_mseries_stop()`, failing if either series counted
  an outlier.
