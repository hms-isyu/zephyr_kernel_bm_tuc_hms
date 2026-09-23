# task_preempt, preempting switch via `k_thread_resume()`

Link this test by setting `CONFIG_BENCHMARK_TEST_TASK_SWITCH_PREEMPT=y` in `prj.conf` (Kconfig
choice `BENCHMARK_TEST`, one test case per build) and rebuilding. Terminology:
[../README.md](../README.md).

## General

The switch is triggered by readying a suspended task. A task that suspends itself leaves the
ready queue without joining any wait queue, so this test exercises the ready queue and the
swap chain alone: no wait-queue walk, no synchronisation object in the measured path.

| Scenario | Trigger | Measurement window | Measured path |
| --- | --- | --- | --- |
| A | Transition | start marker in `L_Task_SA`, stop marker in `H_Task_SA` (cross-thread) | `k_thread_resume()` entry, `z_ready_thread()`, `reschedule()`, the swap, and the return half of `H_Task_SA`'s own `k_thread_suspend()` |
| B | Activation API | start and stop marker inside `Task_SB` (same function) | the same `k_thread_resume()` on a task of *lower* priority than the caller: entry, `z_ready_thread()`, `reschedule()` taking the no-swap arm, return |

Everything up to `reschedule()`'s swap decision is common to both scenarios and cancels in
`S_A - S_B`. What is left is the swap chain plus the tail of the suspend call in which S_A
stops. Neither can be timed in the kernel itself, the window would have to open inside the
context switch, so both are measured separately by the compensation probes.

## Threads

| Thread | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | creates and tears down the measured tasks, then spins on the result signal |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 8 | A | suspends itself each round, takes the stop marker when the suspend call returns |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 9 | A | suspends `H_Task_SA`, takes the start marker, resumes it and is preempted |
| `Task_SB` | `TASK_SB_HIGH_PRIO` = 2 | B | suspends and resumes `H_Task_SA` with `Task_SB > H`, and keeps the CPU |
| `sb_dummy_threads[0..n-1]` (`dummy_task_b`) | `TASK_SB_DUMMY_1..4_PRIO`, one band apart, `Task_SB > D > H` | B | ready-queue load, each self-suspends when it runs |
| kernel idle | 16 | both | runs only when no test task is ready |

`D` exists on the S_B side only: `Task_SB > D > H`, one band apart in pool order, so every
dummy outranks the resumed task and is outranked by the caller.
`sa_dummy_threads` / `TASK_SA_DUMMY_*_PRIO` are allocated and defined but never created. `TASK_SB_LOW_PRIO` (2) is defined but unused.

## Measurement

Selected by `TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h).

| Case | Priorities (L / H, S_B caller) | Dummy load | Measures |
| --- | --- | --- | --- |
| `TEST_CASE_I` | 9 / 8, 2 | none | base measurement: the bare resume-driven switch against the bare no-swap resume |
| `TEST_CASE_II` | 9 / 8, 2 | `n` tasks with `Task_SB > D > H`, S_B side only | loaded measurement: the ready-queue insert with tasks queued between the caller and the resumed task |

`DUMMY_TASKS_COUNT` is the sweep parameter of the loaded measurement and is changed between
runs, one build per value, `n` = 1..4 (capped by `DUMMY_POOL_SIZE`). The value committed in
[benchmark_testcases.h](benchmark_testcases.h) is only the point that was measured last.

The loaded measurement changes the S_B side only, the S_A side is identical in both cases.
The dummies are resumed *before* `BMTH_mwindow_open()` and suspended again *after*
`BMTH_mwindow_close()`, so their own resume and suspend cost stays outside the window and only
their presence in the ready queue is inside it.

The scenarios run one after the other: `I_Task` creates the S_A pair; when they have suspended
themselves it aborts `L_Task_SA`, creates the dummy load and `Task_SB`.

## Compensation

Measured before the scenarios start, in `main()`, from [zephyr_sim.c](zephyr_sim.c).

| Probe | Series | What it re-runs without a switch |
| --- | --- | --- |
| `measure_thread_resume_tail()` | `z_swap_overhead` | the return chain of `k_thread_resume()`, the S_B tail |
| `measure_thread_suspend_tail()` | `suspend_tail` | the half of `k_thread_suspend()` that runs after the switch, the part inside S_A's window, since S_A stops the moment the call returns |
| `measure_swap_prologue_overhead()` | `swap_prologue` | the swap chain from its entry to the `irq_unlock()` where PendSV fires; the resume prologue ahead of it is shared with S_B and cancels out |

## Analysis

Documents: [doc/sa_vs_sb.html](doc/sa_vs_sb.html),
[doc/sim_vs_kernel.html](doc/sim_vs_kernel.html).

Queue state at the start marker, the kernel idle task (16) is ready in every row and left out.
*Suspended* means `_THREAD_SUSPENDED`: in no ready queue and in no wait queue. `SB_D1` (3),
`SB_D2` (4), `SB_D3` (5), `SB_D4` (6) are `sb_dummy_threads[]` in pool order,
`n` = `DUMMY_TASKS_COUNT`.

| Case | Scenario | Releasing task | Ready queue | Suspended | Wait queue | After the measured call |
| --- | --- | --- | --- | --- | --- | --- |
| I | A | `L_Task_SA` (9) | `I_Task` (15) | `H_Task_SA` (8) | empty | `k_thread_resume()` appends `H_Task_SA` to band 8, now the highest occupied band, swap, stop marker in `H_Task_SA` as its own `k_thread_suspend()` returns |
| I | B | `Task_SB` (2) | `I_Task` (15) | `H_Task_SA` (8) | empty | `k_thread_resume()` appends `H_Task_SA` to band 8, caller > resumed task, no swap, stop marker in `Task_SB` |
| II | A | `L_Task_SA` (9) | `I_Task` (15) | `H_Task_SA` (8) | empty | as case I, this test carries no dummy load on the S_A side |
| II | B | `Task_SB` (2) | `SB_D1` (3) … `SB_Dn`, `I_Task` (15) | `H_Task_SA` (8) | empty | as case I, with `n` tasks queued between the caller's band 2 and band 8 |

How the dummies load the workload:

- The load lands entirely in the **ready queue**, in the bands between caller and resumed
  task. Before the measured call the dummies are ready and the resumed task is suspended,
  after it the resumed task has been appended to band 8 and nothing else has moved.
- The **wait queue** stays empty in both scenarios. `test_event` carries only the start
  barrier (`START_EVENT_MASK`, 0x0008), on which `H_Task_SA` and `L_Task_SA` pend once before
  the first iteration. `SCENARIO_A_EVENT_MASK` and `SIGNALIZE_YIELD_EVENT_MASK` are defined
  but never posted.
- `H_Task_SA` is the resumed task in both scenarios. `Task_SB` re-suspends it at the top of
  every S_B round, so it never actually runs while S_B is measuring.
- Suspension is a thread state, not a queue. This is what makes the test the pure ready-queue
  counterpart to [../sem_preempt](../sem_preempt) and [../event_preempt](../event_preempt),
  where the same switch is driven out of a wait queue.

## Configuration

Shared configuration: [../README.md](../README.md). Nothing in this test deviates from it.

This is the test where the ready-queue implementation matters most. Under `SCHED_MULTIQ` the
`z_ready_thread()` of the resumed task is an O(1) append to its own band, so the dummy load is
expected not to change the measured value. Under `CONFIG_SCHED_SIMPLE` the ready queue is a
single sorted list and the insert walks every ready task that outranks the resumed one, so the
same sweep loads the measured path directly.
