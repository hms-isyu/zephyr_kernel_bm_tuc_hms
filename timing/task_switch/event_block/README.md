# event_block, blocking switch via `k_event_wait_safe()`

Link this test by setting `CONFIG_BENCHMARK_TEST_EVENT_TASK_SWITCH_BLOCK=y` in `prj.conf`
(Kconfig choice `BENCHMARK_TEST`, one test case per build) and rebuilding. Terminology:
[../README.md](../README.md).

## General

The mirror image of [../event_preempt](../event_preempt). The switch is not caused by a post
that wakes somebody, but by the waiter itself giving up the CPU. `H_Task_SA` takes the start
marker and calls `k_event_wait_safe()` on a bit that is not set, so the call runs into
`z_pend_curr()`: the task is inserted into the priority-sorted wait queue of the object and
the scheduler hands the CPU to the next ready task, which takes the stop marker. Here `H` is
the measured task and `L` only collects the marker.

| Scenario | Trigger | Measurement window | Measured path |
| --- | --- | --- | --- |
| A | Transition (blocking) | start marker in `H_Task_SA`, stop marker in `L_Task_SA` (cross-thread) | `k_event_wait_safe()` entry, the condition test, `z_pend_curr()` with the priority-ordered insert into the wait queue, `reschedule()`, the swap, and the resumption of `L_Task_SA` |
| B | Activation API | start and stop marker inside `H_Task_SB` (same function) | the same call with `K_NO_WAIT` on a bit that is never posted: entry, condition test, unlock, return. It never pends and never reaches the scheduler |

`S_A - S_B` isolates the pend itself: the wait-queue insert, the reschedule, the swap, and the
entry into the next task. Because scenario A stops in a *different* task than it starts in,
the value also contains the part of `L_Task_SA` up to its counter read, which the compensation
probes account for.

## Threads

| Thread | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | consumes `SIGNALIZE_YIELD_EVENT_MASK` and resumes the driver task once per round, tears the scenarios down |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 6 | A | opens the window, takes the start marker and blocks in `k_event_wait_safe()`, the measured task |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | A | takes the stop marker when it gets the CPU, posts `SCENARIO_A_EVENT_MASK` to re-arm `H_Task_SA`, then self-suspends |
| `sa_dummy_threads[0..n-1]` (`dummy_task`) | `TASK_SA_DUMMY_1..4_PRIO`, one band apart, `D > H` | A | wait-queue load, each pends on `SCENARIO_A_EVENT_MASK` |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | B | takes both markers around a `K_NO_WAIT` wait, self-suspends at the end of each round |
| kernel idle | 16 | both | runs only when no test task is ready |

`D > H > L`. This is the inverse of the preempting tests: the load must be enqueued ahead of
the measured task in the queue it inserts itself into, so that the insert has to walk past it. `sb_dummy_threads`, `TASK_SB_DUMMY_*_PRIO` and
`TASK_SB_LOW_PRIO` are allocated and defined but unused, there is no S_B low priority task
and no dummy load on the S_B side.

## Measurement

Selected by `TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h). The priorities are
the same in both populated cases (S_A 7 / 6, S_B 8).

| Case | Dummy load | Measures |
| --- | --- | --- |
| `TEST_CASE_I` | none | base measurement: the bare pend, insert into an empty wait queue, reschedule, swap |
| `TEST_CASE_II` | `n` tasks with `D > H` | loaded measurement: the same pend with the queue loaded ahead of the waiter |

`DUMMY_TASKS_COUNT` is the sweep parameter of the loaded measurement and is changed between
runs, one build per value, `n` = 1..4 (capped by `DUMMY_POOL_SIZE`). The value committed in
[benchmark_testcases.h](benchmark_testcases.h) is only the point that was measured last.

`TEST_CASE_III`..`TEST_CASE_VI` are enumerated in the header but carry no definitions.

The scenarios run one after the other: `I_Task` drives S_A for `MEASUREMENT_COUNT` iterations,
aborts the S_A tasks, clears `SCENARIO_A_EVENT_MASK`, then creates `H_Task_SB` and drives it
the same way.

## Compensation

Measured before the scenarios start, in `main()`, from [zephyr_sim.c](zephyr_sim.c). Every
simulated chain is cut at the `irq_unlock()` where the kernel pends PendSV and the task leaves
the CPU (see [sim_measure.h](sim_measure.h)).

| Probe | Series | What it re-runs without a switch |
| --- | --- | --- |
| `measure_event_z_swap_overhead(SIM_Z_SWAP_PROLOGUE)` | `z_swap_overhead` | the swap chain from entry to the `irq_unlock()` where PendSV would fire |
| `measure_wait_tail_no_wait_overhead()` | `wait_tail_no_wait` | the tail of `k_event_wait_safe()` for a `K_NO_WAIT` wait, which returns without pending, the scenario B counterpart |

## Analysis

Documents: [doc/sa_vs_sb.html](doc/sa_vs_sb.html),
[doc/sim_vs_kernel.html](doc/sim_vs_kernel.html).

Queue state at the start marker, the kernel idle task (16) is ready in every row and left out.
The wait queue is the queue of `test_event` that the blocking task inserts itself into.
`SA_D1` (2), `SA_D2` (3), `SA_D3` (4), `SA_D4` (5) are `sa_dummy_threads[]` in pool order,
`n` = `DUMMY_TASKS_COUNT`.

| Case | Scenario | Blocking task | Ready queue | Wait queue (`test_event`) | After the measured call |
| --- | --- | --- | --- | --- | --- |
| I | A | `H_Task_SA` (6) | `L_Task_SA` (7), `I_Task` (15) | empty | the wait inserts `H_Task_SA` into the empty queue, band 6 empties, `_priq_run_best` returns `L_Task_SA`, swap, stop marker in `L_Task_SA` |
| I | B | `H_Task_SB` (8) | `I_Task` (15) | empty | the `K_NO_WAIT` wait tests the bit and returns without inserting, stop marker in `H_Task_SB` |
| II | A | `H_Task_SA` (6) | `L_Task_SA` (7), `I_Task` (15) | `SA_D1` (2) … `SA_Dn` | the insert walks the `n` dummy nodes before placing `H_Task_SA` behind them, then as case I, stop marker in `L_Task_SA` |
| II | B | `H_Task_SB` (8) | `I_Task` (15) | empty | as case I, the dummy load exists on the S_A side only |

How the dummies load the workload:

- The load changes the **wait queue**, not the ready queue. Each dummy was woken by
  `L_Task_SA`'s post, closed the `scenario_a` window and re-pended before `H_Task_SA` got the
  CPU, so at the start marker all `n` of them are back in the queue and `H_Task_SA` has to
  walk past them. After the measured call the queue holds the `n` dummies plus `H_Task_SA`
  behind them, and the ready queue has lost band 6.
- `D > H` is deliberate. `z_priq_simple_add()` walks only the entries the inserted task does
  not outrank, so `D > H` places the whole load ahead of the measured waiter and makes the
  insert cost scale with the load.
- Scenario B never inserts, so its wait queue stays empty. `I_Task` clears
  `SCENARIO_A_EVENT_MASK` and aborts the S_A tasks before the scenario starts.
- `dummy_task` closes the `scenario_a` window, so an iteration in which a dummy ran after
  `H_Task_SA` opened it is discarded.

`test_event` carries every signal in the test:

| Mask | Direction | In the measurement window |
| --- | --- | --- |
| `SCENARIO_A_EVENT_MASK` (0x0010) | the bit `H_Task_SA` and the S_A dummies wait on, posted by `L_Task_SA` | yes, the measured pend of scenario A is the insert into this queue |
| `SCENARIO_B_EVENT_MASK` (0x0002) | never posted | yes, the `K_NO_WAIT` wait of scenario B tests it and returns |
| `SIGNALIZE_YIELD_EVENT_MASK` (0x0004) | driver task to `I_Task` | no, posted after the window closes |
| `START_EVENT_MASK` (0x0008) | start barrier | no, before the first iteration |

Round order in scenario A: `I_Task` resumes `L_Task_SA`, `L_Task_SA` posts
`SCENARIO_A_EVENT_MASK`, the dummies (bands 2..5) run first, each closing the `scenario_a`
window and re-pending, `H_Task_SA` (band 6) runs, re-opens the window, takes the start marker
and blocks, `L_Task_SA` (band 7) takes the stop marker at the top of its loop, closes the
window, posts `SIGNALIZE_YIELD_EVENT_MASK` and self-suspends, `I_Task` runs again.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option (`prj.conf`) | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_EVENTS` | y | `k_event` available |
| `CONFIG_WAITQ_SIMPLE` | y | `test_event` owns one priority-sorted list (`z_priq_simple_*`), the pend measured here walks it from the head until it meets a task the pending one outranks |
