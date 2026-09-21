# event_preempt — preempting switch via `k_event_post()`

Sources: [time_task_switch_event.c](time_task_switch_event.c), [zephyr_sim.c](zephyr_sim.c),
[sim_measure.h](sim_measure.h), [benchmark_testcases.h](benchmark_testcases.h)
Linked when `CONFIG_BENCHMARK_TEST_EVENT_TASK_SWITCH_PREEMPT=y` (Kconfig choice
`BENCHMARK_TEST`).

## General description

The switch is triggered from a wait queue: a higher priority thread is already pending in
`k_event_wait_safe()` when a lower priority thread posts the bit it waits for.
`k_event_post()` walks the *entire* wait queue of the object (`z_sched_waitq_walk()`), tests
every waiter mask, readies each waiter that matches, and calls `z_reschedule()` once at the
end of the walk.

| Series | Window | Measured chain |
| --- | --- | --- |
| `scenario_a` (S_A) | start marker in `L_Task_SA`, stop marker in `H_Task_SA` (cross-thread read window) | `k_event_post()` entry, the walk over the whole wait queue, the ready-add of every matched waiter, `z_reschedule()`, the swap, and the tail of `k_event_wait_safe()` in `H_Task_SA` |
| `scenario_b` (S_B) | start and stop marker inside `H_Task_SB` (same-function read window) | the same post where no matched waiter can preempt the poster: entry, walk, ready-adds, `z_reschedule()` on the no-swap arm, return |

`S_A - S_B` leaves the swap and the wait tail when S_B has a waiter
(`TEST_SB_HAS_WAITER == 1`), and additionally the walk and the ready-adds when it has none
(`TEST_SB_HAS_WAITER == 0` with no dummy load, where the post never reaches the scheduler).

`k_event_wait_safe()` is the consuming variant (`K_EVENT_OPTION_CLEAR`): a woken waiter
clears the bits it matched, so every round starts from the same event state and the waiters
re-pend on a bit that is not set.

The two series run one after the other: `I_Task` drives S_A for `MEASUREMENT_COUNT`
iterations, aborts the S_A threads, then creates the S_B set and drives it the same way.

## Kernel configuration behind the queues

| Option (`prj.conf`) | Value | Consequence for the queues |
| --- | --- | --- |
| `CONFIG_MP_MAX_NUM_CPUS` | 1 | one ready queue; exactly one thread runs |
| `CONFIG_SCHED_MULTIQ` | y | ready queue = one FIFO list per priority plus a bitmask (`z_priq_mq_*`); add, remove and "pick best" are O(1) and independent of occupancy |
| `CONFIG_WAITQ_SIMPLE` | y | `test_event` owns one priority-sorted list (`z_priq_simple_*`); a pend walks it from the head, a post walks all of it |
| `CONFIG_TIMESLICING` | n | no involuntary rotation inside a band |
| `CONFIG_EVENTS` | y | `k_event` available |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | priorities 0..15, all preemptible; the kernel idle thread sits at `K_IDLE_PRIO` = 16, below every test thread |

Lower number = higher priority. *Band* = the ready-queue list of one priority. A thread that
calls `k_thread_suspend()` leaves the ready queue and joins no wait queue.

## Threads

| Thread | Priority | Phase | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | consumes `SIGNALIZE_YIELD_EVENT_MASK` and resumes the driver thread once per round; tears the phases down |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 2 | S_A | pends on `SCENARIO_A_EVENT_MASK`; takes the stop marker when the wait returns |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | S_A | takes the start marker, posts `SCENARIO_A_EVENT_MASK`, is preempted, then self-suspends |
| `sa_dummy_threads[0..n-1]` (`dummy_task`) | `TASK_SA_DUMMY_1..4_PRIO` = 3, 4, 5, 6 | S_A | wait-queue load: each pends on `SCENARIO_A_EVENT_MASK`, so each is matched, woken and re-pended every round |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | S_B | posts `SCENARIO_B_EVENT_MASK` and takes both markers; self-suspends at the end of each round |
| `L_Task_SB` | `TASK_SB_LOW_PRIO` = 14 | S_B | with `TEST_SB_HAS_WAITER == 1` pends on `SCENARIO_B_EVENT_MASK`; otherwise self-suspends and is resumed explicitly |
| `sb_dummy_threads[0..n-1]` (`dummy_task`) | `TASK_SB_DUMMY_1..4_PRIO` = 10, 11, 12, 13 | S_B | wait-queue load pending on `SCENARIO_B_EVENT_MASK` |
| kernel idle | 16 | both | runs only when no test thread is ready |

## Queue snapshots at the start marker

One row per series per test case, read as
*posting task [ … ] — ready queue [ … ] — wait queue [ … ]*:

- **Posting task** — the thread that holds the CPU, takes the start marker and calls
  `k_event_post()`.
- **Ready queue** — the other ready threads, highest priority first, `(priority)` after each
  name.
- **Wait queue** — the threads pending on `test_event`, in queue order (priority-sorted).
- **Effect** — what the post does to the queues, and where the stop marker is taken.

The kernel idle thread (`K_IDLE_PRIO` = 16) is ready in every row and is left out.
`SA_D1`…`SA_D4` are `sa_dummy_threads[]` (3, 4, 5, 6) and `SB_D1`…`SB_D4` are
`sb_dummy_threads[]` (10, 11, 12, 13), each created in pool order.

| Case | Series | Posting task | Ready queue | Wait queue (`test_event`) | Effect |
| --- | --- | --- | --- | --- | --- |
| I | S_A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2) | walks 1 node, wakes `H_Task_SA` into band 2 → swap → stop marker in `H_Task_SA` |
| I | S_B | `H_Task_SB` (8) | `I_Task` (15) | `L_Task_SB` (14) | walks 1 node, wakes `L_Task_SB` into band 14, below the poster → no swap → stop marker in `H_Task_SB` |
| II | S_A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2) | as case I |
| II | S_B | `H_Task_SB` (8) | `L_Task_SB` (14), `I_Task` (15) | — empty | no waiter at all: no walk, no ready-add, the scheduler is never reached → stop marker in `H_Task_SB` |
| III | S_A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2), `SA_D1` (3), `SA_D2` (4), `SA_D3` (5), `SA_D4` (6) | walks 5 nodes, wakes all 5 into bands 2..6 → swap to band 2 → stop marker in `H_Task_SA` |
| III | S_B | `H_Task_SB` (8) | `I_Task` (15) | `SB_D1` (10), `SB_D2` (11), `SB_D3` (12), `SB_D4` (13), `L_Task_SB` (14) | walks 5 nodes, wakes all 5, every one below the poster → no swap → stop marker in `H_Task_SB` |
| IV | S_A | `L_Task_SA` (7) | `I_Task` (15) | `H_Task_SA` (2), `SA_D1` (3), `SA_D2` (4), `SA_D3` (5) | walks 4 nodes, wakes all 4 into bands 2..5 → swap to band 2 → stop marker in `H_Task_SA` |
| IV | S_B | `H_Task_SB` (8) | `L_Task_SB` (14), `I_Task` (15) | `SB_D1` (10), `SB_D2` (11), `SB_D3` (12) | walks 3 nodes, wakes all 3, every one below the poster → no swap → stop marker in `H_Task_SB` |

Reading the rows:

- The post walks the whole queue and wakes *every* matching waiter, so both the walk length
  and the number of ready-adds scale with `DUMMY_TASKS_COUNT`. This is the difference to
  [../sem_preempt](../sem_preempt), where `k_sem_give()` pops the head only.
- `z_reschedule()` runs once, after the walk, so all ready-adds are already inside the window
  when the swap decision is taken.
- With `TEST_SB_HAS_WAITER == 0` (cases II and IV) `L_Task_SB` does not pend; `H_Task_SB`
  resumes it before opening the window, which is why it appears in the ready queue there.
- `I_Task` is *ready*, not pending, in every row: it is preempted inside `k_thread_resume()`
  and finds `SIGNALIZE_YIELD_EVENT_MASK` already set on its next pass, so it holds no node in
  the wait queue while the measured post walks it.

`test_event` carries every signal in the test:

| Mask | Direction | In the measured path? |
| --- | --- | --- |
| `SCENARIO_A_EVENT_MASK` (0x0001) | `L_Task_SA` → `H_Task_SA` + S_A dummies | yes — the measured post of S_A |
| `SCENARIO_B_EVENT_MASK` (0x0002) | `H_Task_SB` → `L_Task_SB` + S_B dummies | yes — the measured post of S_B |
| `SIGNALIZE_YIELD_EVENT_MASK` (0x0004) | driver thread → `I_Task` | no — posted after the window closes |
| `START_EVENT_MASK` (0x0008) | start barrier | no — before the first iteration |

Round order in S_A: `I_Task` resumes `L_Task_SA` and is preempted → `L_Task_SA` posts →
`H_Task_SA` (band 2) runs first, stops the window, posts `SIGNALIZE_YIELD_EVENT_MASK` and
re-pends → the dummies (bands 3..6) run and re-pend → `L_Task_SA` returns from the post and
self-suspends → `I_Task` regains the CPU and resumes `L_Task_SA` again.

## Test cases

Selected by `TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h). The priorities are
the same in all four cases (S_A 7 / 2, S_B 8 / 14).

| Case | `TEST_SB_HAS_WAITER` | `DUMMY_TASKS_COUNT` | What it isolates |
| --- | --- | --- | --- |
| `TEST_CASE_I` | 1 | 0 | switch + wait tail, against a post that walks one waiter and does not switch |
| `TEST_CASE_II` | 0 | 0 | switch + wait tail + walk + ready-add, against a post that reaches nothing |
| `TEST_CASE_III` | 1 | 4 | the same as I with four extra waiters on both sides |
| `TEST_CASE_IV` | 0 | 3 | walk and ready-add cost against waiter count, with no switch on the S_B side |

## Compensation probes

Measured before the series start, in `main()`, from [zephyr_sim.c](zephyr_sim.c). Every
simulated chain is cut at the `irq_unlock()` where the kernel pends PendSV and the thread
leaves the CPU (see [sim_measure.h](sim_measure.h)).

| Probe | Series | What it re-runs without a switch |
| --- | --- | --- |
| `measure_event_tail_overhead()` | `event_tail` | the tail of `k_event_post()` for a post with no matching waiter |
| `measure_wait_overhead(SIM_WAIT_TAIL)` | `wait_tail` | the half of `k_event_wait_safe()` after the PendSV point, where the waiter gets the CPU back |
| `measure_event_z_swap_overhead(SIM_Z_SWAP_PROLOGUE)` | `z_swap_overhead` | the swap chain from entry to the `irq_unlock()` where PendSV would fire |

## Measurement bookkeeping

- `MEASUREMENT_COUNT` = 50000 iterations per series.
- `BMTH_mwindow_open()` / `BMTH_mwindow_close()` gate `BMTH_mseries_iterate()`: an iteration
  is only recorded while the window is open.
- `scenario_a` is initialised as a cross-function read window, `scenario_b` as an
  inside-function one, matching where their markers are taken.
- Both series call `BMTH_signalize_jitter_detected()` on a jittered window; `I_Task` reports
  through `BMTH_signalize_mseries_stop()`, failing if either series counted an outlier.
