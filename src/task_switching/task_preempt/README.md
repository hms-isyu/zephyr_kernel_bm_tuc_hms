# task_preempt, preempting switch via `k_thread_resume()`

Set `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_TASK_PREEMPT=y` in `prj.conf`. `#define TEST_CASE`
in `benchmark_testcases.h` selects `TEST_CASE_I` or `TEST_CASE_II`.

[../README.md](../README.md)

`k_thread_resume()` makes a suspended task ready and switches to it if the resumed task
outranks the caller. This test case times `k_thread_resume()` on `H_Task_SA`, called by
`L_Task_SA`, which `H_Task_SA` outranks, against the same call by `Task_SB`, which outranks
`H_Task_SA`.

## Participants

| Participant | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | control task |
| `L_Task_SA` (`sa_low_prio_thread`) | `TASK_SA_LOW_PRIO` = 9 | `S_A` | takes the start marker, calls `k_thread_resume(&sa_high_prio_thread)` |
| `H_Task_SA` (`sa_high_prio_thread`) | `TASK_SA_HIGH_PRIO` = 8 | both | `S_A`: takes the stop marker on return from its own `k_thread_suspend()`. `S_B`: suspended and resumed by `Task_SB`, never runs. |
| `Task_SB` (`sb_thread`) | `TASK_SB_HIGH_PRIO` = 2 | `S_B` | takes both markers around `k_thread_resume(&sa_high_prio_thread)` |
| `sb_dummy_threads[i]` (`dummy_task_b`) | `Task_SB > D > H_Task_SA` | `S_B` | ready tasks during the window, suspended outside it |

## Measurement

In both scenarios `k_thread_resume()` finds `H_Task_SA` suspended and makes it ready.
`H_Task_SA > L_Task_SA` hands the CPU to `H_Task_SA` in `S_A`; `Task_SB > H_Task_SA` keeps
`Task_SB` on the CPU in `S_B`.

| Scenario | Measurement window | What the window holds |
| --- | --- | --- |
| `S_A` | start marker in `L_Task_SA`, stop marker in `H_Task_SA` | the ready queue insert of `H_Task_SA`, the switch, the tail of the `k_thread_suspend()` of `H_Task_SA` |
| `S_B` | both markers in `Task_SB` | the ready queue insert of `H_Task_SA`, the tail of `k_thread_resume()` |

`S_A - S_B` holds the prologue of the switch, the switch and the tail of the
`k_thread_suspend()` of `H_Task_SA`, less the tail of `k_thread_resume()`. In a base
measurement the ready queue insert of `H_Task_SA` is the same on both paths and cancels.

| Test case | Parameters | Measures |
| --- | --- | --- |
| `TEST_CASE_I` | `ADD_DUMMY_TASKS` = 0, `dummy_count` = 0 | base measurement |
| `TEST_CASE_II` | `ADD_DUMMY_TASKS` = 1, `DUMMY_TASKS_COUNT` dummy tasks with `Task_SB > D > H_Task_SA`, in `S_B` only | loaded measurement |

`DUMMY_TASKS_COUNT` is the load size, swept one build per value over `0..DUMMY_POOL_SIZE` in the
`TEST_CASE_II` branch. No script runs the sweep: before each build, set `#define DUMMY_TASKS_COUNT`
in that branch of `benchmark_testcases.h` by hand. At `DUMMY_TASKS_COUNT` = 0,
`TEST_CASE_II` creates the same tasks as `TEST_CASE_I`. The dummy tasks are ready only during
the `S_B` window, so the sweep acts on `S_B` alone and `S_A - S_B` follows it through `S_B` alone.

`I_Task` aborts `L_Task_SA` after `S_A`, keeps the suspended `H_Task_SA`,
then creates the dummy tasks and `Task_SB`.

## Compensation

| Function | Simulates | Belongs to |
| --- | --- | --- |
| `measure_swap_prologue_overhead()` | the prologue of the switch | `k_thread_resume()`, `S_A` side |
| `measure_thread_suspend_tail()` | the tail of `k_thread_suspend()` of a task that suspended itself | `k_thread_suspend()` of `H_Task_SA`, `S_A` side |
| `measure_thread_resume_tail()` | the tail of `k_thread_resume()` without the switch | `k_thread_resume()`, `S_B` side |

## Analysis

[doc/sa_vs_sb.html](doc/sa_vs_sb.html): every instruction of both windows, in execution order,
from the linked `zephyr.elf`. [doc/sim_vs_kernel.html](doc/sim_vs_kernel.html): each simulated
function set against the kernel code it re-runs.

## Configuration

General configuration: [../README.md](../README.md#configuration).

| Option | Meaning |
| --- | --- |
| `CONFIG_SCHED_MULTIQ` | selects the ready queue insert of `H_Task_SA`, and with it whether `S_B` depends on `DUMMY_TASKS_COUNT` |
