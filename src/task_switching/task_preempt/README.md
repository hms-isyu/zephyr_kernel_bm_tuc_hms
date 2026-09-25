# task_preempt, preempting switch via `k_thread_resume()`

Build: set `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_TASK_PREEMPT=y` in `prj.conf`, one test case
per build. `#define TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h) selects
`TEST_CASE_I` or `TEST_CASE_II`. The point of the sweep is `#define DUMMY_TASKS_COUNT` in the
`TEST_CASE_II` branch of that file. Terms, scenarios and shared configuration:
[../README.md](../README.md).

`k_thread_resume()` makes a suspended task ready again and switches to it if it outranks the
caller. This test case times `k_thread_resume()` on `H_Task_SA` called by `L_Task_SA`, which
`H_Task_SA` outranks, against the same call by `Task_SB`, which outranks `H_Task_SA`. A
suspended task waits in no wait queue (`z_impl_k_thread_suspend()` only removes it from the
ready queue), so the window holds the ready queue alone. In
[../sem_preempt](../sem_preempt) and [../event_preempt](../event_preempt) the woken task comes
out of a wait queue, through `z_unpend_first_thread()` and `z_sched_waitq_walk()`.

## Participants

| Participant | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | creates `H_Task_SA` and `L_Task_SA`, posts `START_EVENT_MASK`, aborts `L_Task_SA` once both have suspended, then creates `sb_dummy_threads[0..dummy_count-1]` and `Task_SB`. `H_Task_SA` is not aborted and serves `S_B`. |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 9 | `S_A` | opens the window, takes the start marker, calls `k_thread_resume(&sa_high_prio_thread)`. Its `k_thread_suspend(&sa_high_prio_thread)` ahead of that finds `H_Task_SA` already suspended and returns without effect (`z_is_thread_suspended()` in `z_impl_k_thread_suspend()`). |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 8 | both | `S_A`: suspends itself after each round, before the next window opens, and takes the stop marker when that `k_thread_suspend()` returns. `S_B`: suspended by `Task_SB` before each window, resumed inside it, never runs. |
| `Task_SB` | `TASK_SB_HIGH_PRIO` = 2 | `S_B` | per round: resumes the dummy tasks, suspends `H_Task_SA`, opens the window, takes both markers around `k_thread_resume(&sa_high_prio_thread)`, closes the window, suspends the dummy tasks |
| `sb_dummy_threads[i]` (`dummy_task_b`) | `Task_SB > D > H_Task_SA` | `S_B` | suspend themselves when created. Ready during each `S_B` window, suspended outside it. |

The measured operation is `k_thread_resume()` on `H_Task_SA` in both scenarios. It works on the
ready queue only. `sa_dummy_threads[]` is never created, so the dummy tasks act on `S_B` alone.

## Measurement

In both scenarios `k_thread_resume()` finds `H_Task_SA` suspended and makes it ready. In `S_A`
`H_Task_SA > L_Task_SA`, so the call switches to `H_Task_SA`. In `S_B` `Task_SB > H_Task_SA`,
so `Task_SB` keeps the CPU.

| Scenario | Trigger | Measurement window | What the window holds |
| --- | --- | --- | --- |
| `S_A` | `L_Task_SA` calls `k_thread_resume(&sa_high_prio_thread)` | start marker in `L_Task_SA`, stop marker in `H_Task_SA` | `k_thread_resume()` up to `reschedule()`, the arm of `reschedule()` that calls `z_swap()` because `need_swap()` is true, the PendSV exception restoring `H_Task_SA`, the tail of the `k_thread_suspend()` of `H_Task_SA` |
| `S_B` | `Task_SB` calls `k_thread_resume(&sa_high_prio_thread)` | both markers in `Task_SB` | `k_thread_resume()` up to `reschedule()`, the arm of `reschedule()` without `z_swap()` because `need_swap()` is false, the tail of `k_thread_resume()` |

Both paths part at the `need_swap()` test in `reschedule()`. Ahead of it both run
`z_impl_k_thread_resume()` and `ready_thread()`, whose ready queue insert of `H_Task_SA`
differs by `CONFIG_SCHED_MULTIQ`:

| `CONFIG_SCHED_MULTIQ` | Insert of `H_Task_SA` | `S_A - S_B` |
| --- | --- | --- |
| n | `z_priq_simple_add()` passes the ready tasks `H_Task_SA` does not outrank. `S_A`: none, `L_Task_SA` is first. `S_B`: `Task_SB` and `dummy_count` dummy tasks. | the part after the `need_swap()` test, less `dummy_count + 1` steps of the insert walk |
| y | `z_priq_mq_add()`, `O(1)` in both scenarios | the part after the `need_swap()` test |

After the `need_swap()` test, `S_A` holds the `z_swap()` prologue, the PendSV exception and the
tail of `k_thread_suspend()`, and `S_B` holds the unlock and the tail of `k_thread_resume()`.

| Test case | Parameters | Measures |
| --- | --- | --- |
| `TEST_CASE_I` | `ADD_DUMMY_TASKS` = 0, `dummy_count` = 0 | base measurement |
| `TEST_CASE_II` | `ADD_DUMMY_TASKS` = 1, `DUMMY_TASKS_COUNT` dummy tasks with `Task_SB > D > H_Task_SA`, in `S_B` only | loaded measurement |

The sweep parameter is `DUMMY_TASKS_COUNT`, swept one build per value over `0..DUMMY_POOL_SIZE`
in the `TEST_CASE_II` branch. At `DUMMY_TASKS_COUNT` = 0, `TEST_CASE_II` creates the same tasks
as `TEST_CASE_I`. The dummy tasks act on `scenario_b` only, so `S_A - S_B` follows the sweep
only through `scenario_b`. `Task_SB` resumes them before `BMTH_mwindow_open()` and suspends them
after `BMTH_mwindow_close()`: inside the window they are ready tasks, and their own resume and
suspend calls stay outside it.

`S_A` runs first. `I_Task` aborts `L_Task_SA` after `S_A`, keeps the suspended `H_Task_SA`,
then creates the dummy tasks and `Task_SB`.

## Compensation

| Probe | Series | Re-runs without the switch | May be set against |
| --- | --- | --- | --- |
| `measure_swap_prologue_overhead()` | `swap_prologue` | the `z_swap()` prologue, from its entry to the `irq_unlock()` in `arch_swap()` | the `S_A` side after the `need_swap()` test |
| `measure_thread_suspend_tail()` | `suspend_tail` | the tail of `k_thread_suspend()` of a task that suspended itself | the `S_A` side after the PendSV exception |
| `measure_thread_resume_tail()` | `z_swap_overhead` | the tail of `k_thread_resume()` on the arm of `reschedule()` without `z_swap()`, from its `irq_unlock()` | the `S_B` side after the `need_swap()` test |

The series name `z_swap_overhead` is the identifier in the source; the series holds the tail of
`k_thread_resume()`. No probe re-runs the PendSV exception itself.

## Analysis

[doc/sa_vs_sb.html](doc/sa_vs_sb.html): every instruction of both windows, in execution order,
from the linked `zephyr.elf`. [doc/sim_vs_kernel.html](doc/sim_vs_kernel.html): each probe set
against the kernel code it re-runs.

Expected of the base measurement: `S_A - S_B` is expected to be `swap_prologue`, the PendSV
exception and `suspend_tail`, less `z_swap_overhead` and the unlock ahead of it, and, at
`CONFIG_SCHED_MULTIQ` = n, less one insert walk step.

Expected of the loaded measurement: at `CONFIG_SCHED_MULTIQ` = n, `scenario_b` is expected to
carry a term in `DUMMY_TASKS_COUNT` and `scenario_a` none, so `S_A - S_B` falls with
`DUMMY_TASKS_COUNT`. At y, neither series is expected to carry one.

## Configuration

| Option | Value | Consequence special to this test case |
| --- | --- | --- |
| `CONFIG_SCHED_MULTIQ` | n in `prj.conf`, y a separate build | selects the ready queue insert of `H_Task_SA`, and with it whether `scenario_b` depends on `DUMMY_TASKS_COUNT` |
