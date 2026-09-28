# event_preempt, preempting switch via `k_event_post()`

Set `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_EVENT_PREEMPT=y` in `prj.conf`. `TEST_CASE` in
`benchmark_testcases.h` selects one of `TEST_CASE_I`..`TEST_CASE_IV`.

[../README.md](../README.md)

`k_event_post()` sets bits in an event object and wakes every waiting task whose mask the
posted bits match. This test times that post, by `L_Task_SA` in `S_A` and by `H_Task_SB` in
`S_B`, together with the ready-queue insert of every task it wakes; `sem_preempt` times
`k_sem_give()`, whose loaded measurement inserts at most one such task regardless of its load
size.

## Participants

| Name | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | the control task |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 2 | `S_A` | takes the stop marker on return from `k_event_wait_safe()` on `test_event` (`struct k_event`), mask `SCENARIO_A_EVENT_MASK` |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | `S_A` | takes the start marker, calls `k_event_post()` on `test_event`, mask `SCENARIO_A_EVENT_MASK` |
| `sa_dummy_threads[i]` (`dummy_task`) | `H_Task_SA > D > L_Task_SA` | `S_A` | wait on `test_event`, mask `SCENARIO_A_EVENT_MASK`, woken by every `S_A` post |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | `S_B` | takes both markers around `k_event_post()` on `test_event`, mask `SCENARIO_B_EVENT_MASK` |
| `L_Task_SB` | `TASK_SB_LOW_PRIO` = 14 | `S_B` | waits on `test_event`, mask `SCENARIO_B_EVENT_MASK`, when `TEST_SB_HAS_WAITER` = 1, otherwise suspended and outside the window |
| `sb_dummy_threads[i]` (`dummy_task`) | `H_Task_SB > D > L_Task_SB` | `S_B` | wait on `test_event`, mask `SCENARIO_B_EVENT_MASK`, woken by every `S_B` post |

## Measurement

Each iteration cycle, `L_Task_SA` calls `k_event_post()` on `test_event` with mask `SCENARIO_A_EVENT_MASK`
to trigger `S_A`; `H_Task_SB` calls `k_event_post()` on `test_event` with mask
`SCENARIO_B_EVENT_MASK` to trigger `S_B`.

| Scenario | Measurement window | What the window holds |
| --- | --- | --- |
| `S_A` | start marker in `L_Task_SA`, stop marker in `H_Task_SA` | `k_event_post()`, the wake and ready-queue insert of `H_Task_SA` and `dummy_count` dummy tasks, the switch, the tail of `k_event_wait_safe()` |
| `S_B` | both markers in `H_Task_SB` | `k_event_post()`, the wake and ready-queue insert of the matched waiters of `test_event`, the tail of `k_event_post()` |

`S_A - S_B` isolates the switch to `H_Task_SA` and the tail of `k_event_wait_safe()`. It does
not isolate the wake and ready-queue insert of the matched waiters, which both paths carry
alike.

| Test case | `TEST_SB_HAS_WAITER` | `ADD_DUMMY_TASKS` | Measures |
| --- | --- | --- | --- |
| `TEST_CASE_I` | 1 | 0 | base measurement |
| `TEST_CASE_II` | 0 | 0 | base measurement |
| `TEST_CASE_III` | 1 | 1 | loaded measurement of `TEST_CASE_I` |
| `TEST_CASE_IV` | 0 | 1 | loaded measurement of `TEST_CASE_II` |

The sweep parameter is `DUMMY_TASKS_COUNT`, swept one build per value over `0..DUMMY_POOL_SIZE`
in the `TEST_CASE_III` and `TEST_CASE_IV` branches. No script runs the sweep: before each
build, set `#define DUMMY_TASKS_COUNT` in the branch of the built test case in
`benchmark_testcases.h` by hand. At `DUMMY_TASKS_COUNT` = 0, `TEST_CASE_III`
creates the same tasks as `TEST_CASE_I`, and `TEST_CASE_IV` the same as `TEST_CASE_II`. Each
dummy task is a matched waiter of the post inside the window, so the output value carries one
more wake and ready-queue insert per step of the sweep, in `S_A` and `S_B` alike.

The first `S_A` iteration cycle runs when `L_Task_SA` is created, so `I_Task` resumes `L_Task_SA`
`MEASUREMENT_COUNT - 1` times. `H_Task_SB` suspends itself before each iteration cycle, so `I_Task`
resumes it `MEASUREMENT_COUNT` times.

## Compensation

| Simulated tail or prologue | Series | Function |
| --- | --- | --- |
| the prologue of the switch | `z_swap_overhead` | `measure_event_z_swap_overhead(SIM_Z_SWAP_PROLOGUE)` |
| the tail of `k_event_wait_safe()` | `wait_tail` | `measure_wait_overhead(SIM_WAIT_TAIL)` |
| the tail of `k_event_post()` without the switch | `event_tail` | `measure_event_tail_overhead()` |

## Analysis

Documents, each an instruction listing of the windows from the linked `zephyr.elf`, built at
`CONFIG_SCHED_MULTIQ` = n: `sa_vs_sb_case_<case>.html` for one test case,
`_d1`..`_d3` for `dummy_count` = 1..3, `case_I_III` and `case_II_IV` for the complete window
at `TEST_SB_HAS_WAITER` = 1 and 0, `sim_vs_kernel.html` for each simulated tail or prologue
against the kernel code it re-runs: [doc/sa_vs_sb_case_I.html](doc/sa_vs_sb_case_I.html),
[doc/sa_vs_sb_case_II.html](doc/sa_vs_sb_case_II.html),
[doc/sa_vs_sb_case_I_III.html](doc/sa_vs_sb_case_I_III.html),
[doc/sa_vs_sb_case_II_IV.html](doc/sa_vs_sb_case_II_IV.html),
[doc/sa_vs_sb_case_III_d1.html](doc/sa_vs_sb_case_III_d1.html),
[doc/sa_vs_sb_case_III_d2.html](doc/sa_vs_sb_case_III_d2.html),
[doc/sa_vs_sb_case_III_d3.html](doc/sa_vs_sb_case_III_d3.html),
[doc/sa_vs_sb_case_IV_d1.html](doc/sa_vs_sb_case_IV_d1.html),
[doc/sa_vs_sb_case_IV_d2.html](doc/sa_vs_sb_case_IV_d2.html),
[doc/sa_vs_sb_case_IV_d3.html](doc/sa_vs_sb_case_IV_d3.html),
[doc/sim_vs_kernel.html](doc/sim_vs_kernel.html).

## Configuration

General configuration: [../README.md](../README.md#configuration).
