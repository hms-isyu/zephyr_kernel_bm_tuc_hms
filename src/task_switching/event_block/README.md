# event_block, blocking switch via `k_event_wait_safe()`

Build: set `CONFIG_BENCHMARK_TEST_TASK_SWITCHING_EVENT_BLOCK=y` in `prj.conf`, one test case per
build. `#define TEST_CASE` in [benchmark_testcases.h](benchmark_testcases.h) selects
`TEST_CASE_I` or `TEST_CASE_II`. The point of the sweep is `#define DUMMY_TASKS_COUNT` in the
`TEST_CASE_II` branch of that file. Terms, scenarios and shared configuration:
[../README.md](../README.md).

`k_event_wait_safe()` waits for bits of an event object. When the bits are clear and the
timeout is not `K_NO_WAIT`, it pends the calling task in the wait queue of the object. This test
case times the wait by `H_Task_SA` that pends it and hands the CPU to `L_Task_SA`,
`H_Task_SA > L_Task_SA`, against the wait by `H_Task_SB` with `K_NO_WAIT`, which returns. The
measured task gives up the CPU in its own call. In [../event_preempt](../event_preempt) the
measured call is `k_event_post()` on `test_event`, which wakes a waiting task that outranks the
caller, `H_Task_SA > D > L_Task_SA`. Here `D > H_Task_SA > L_Task_SA`, so the dummy tasks wait
ahead of `H_Task_SA` in the wait queue its own call inserts it into.

## Participants

| Name | Priority | Scenario | Role |
| --- | --- | --- | --- |
| `I_Task` | `TASK_IDLE_PRIO` = 15 | both | creates `sa_dummy_threads[0..dummy_count-1]`, `H_Task_SA` and `L_Task_SA`, posts `START_EVENT_MASK`, resumes `L_Task_SA` once per round after `SIGNALIZE_YIELD_EVENT_MASK`, aborts the `S_A` tasks, creates `H_Task_SB`, posts `START_EVENT_MASK`, resumes `H_Task_SB` once per round. Each round posts `SIGNALIZE_YIELD_EVENT_MASK` before `I_Task` waits on it, so during a measured wait `I_Task` is ready and waits in no wait queue. |
| `H_Task_SA` | `TASK_SA_HIGH_PRIO` = 6 | `S_A` | waits once on `START_EVENT_MASK`, then per round opens the window, takes the start marker, calls `k_event_wait_safe(SCENARIO_A_EVENT_MASK, false, K_FOREVER)` |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` = 7 | `S_A` | takes the stop marker, closes the window, posts `SIGNALIZE_YIELD_EVENT_MASK`, suspends itself. Resumed, it posts `SCENARIO_A_EVENT_MASK`, which wakes `H_Task_SA` and the dummy tasks, and resumes in the tail of that `k_event_post()` when `H_Task_SA` pends. |
| `sa_dummy_threads[i]`, entry function `dummy_task` | `D > H_Task_SA` | `S_A` | wait on `SCENARIO_A_EVENT_MASK`. Woken by every `S_A` post, each calls `BMTH_mwindow_close(&scenario_a)` and waits again before `H_Task_SA` runs, since `D > H_Task_SA`. `H_Task_SA` calls `BMTH_mwindow_open()` after that, so no window it opens is closed by a dummy task. |
| `H_Task_SB` | `TASK_SB_HIGH_PRIO` = 8 | `S_B` | waits once on `START_EVENT_MASK`, then per round opens the window and takes both markers around `k_event_wait_safe(SCENARIO_B_EVENT_MASK, false, K_NO_WAIT)`, closes the window, posts `SIGNALIZE_YIELD_EVENT_MASK`, suspends itself |

Both `TEST_CASE` branches define the same priorities. `sb_dummy_threads[]`,
`TASK_SB_DUMMY_1..4_PRIO` and `TASK_SB_LOW_PRIO` are defined and no task is created from them:
`S_B` creates no dummy task. `TEST_SB_HAS_WAITER` is defined and not read by the test source.

The measured operation is
`k_event_wait_safe()` on `test_event`, and in `S_A` the tail of the `k_event_post()` of
`L_Task_SA` on the same object. The data structure is the wait queue of `test_event`.

## Measurement

`L_Task_SA` posts `SCENARIO_A_EVENT_MASK` and wakes `H_Task_SA`. `k_event_wait_safe()` waits
with `K_EVENT_OPTION_CLEAR`, so the woken `H_Task_SA` consumes the bit and its next wait finds it
clear and pends. `SCENARIO_B_EVENT_MASK` is never posted, so the wait of
`H_Task_SB` finds it clear and returns on `K_NO_WAIT`.

| Scenario | Trigger | Measurement window | What the window holds |
| --- | --- | --- | --- |
| `S_A` | `H_Task_SA` calls `k_event_wait_safe()` on the clear `SCENARIO_A_EVENT_MASK` with `K_FOREVER` | start marker in `H_Task_SA`, stop marker in `L_Task_SA` | the call up to `z_pend_curr()`, which inserts `H_Task_SA` into the wait queue of `test_event` behind `dummy_count` dummy tasks and calls `z_swap()`, the PendSV exception restoring `L_Task_SA`, the tail of the `k_event_post()` of `L_Task_SA` |
| `S_B` | `H_Task_SB` calls `k_event_wait_safe()` on the clear `SCENARIO_B_EVENT_MASK` with `K_NO_WAIT` | both markers in `H_Task_SB` | the call up to the `K_NO_WAIT` test, the tail of `k_event_wait_safe()` |

The paths part at the `K_NO_WAIT` test in `k_event_wait_internal()`. `S_A` continues into
`z_pend_curr()`, which calls `z_swap()` without a `need_swap()` test. `S_B` unlocks and returns.
The dummy tasks wait in `test_event` during the window, so the load acts on the insert of
`H_Task_SA` only.

| Test case | `ADD_DUMMY_TASKS` in the branch | Dummy tasks | Measures |
| --- | --- | --- | --- |
| `TEST_CASE_I` | 0 | none | base measurement |
| `TEST_CASE_II` | 1 | `sa_dummy_threads[0..dummy_count-1]`, `D > H_Task_SA` | loaded measurement |

`TEST_CASE` is committed as `TEST_CASE_II`. `TEST_CASE_III` to `TEST_CASE_VI` are defined in
[benchmark_testcases.h](benchmark_testcases.h) and have no branch.

The sweep parameter is `DUMMY_TASKS_COUNT`, swept one build per value over `0..DUMMY_POOL_SIZE`
in the `TEST_CASE_II` branch. At `DUMMY_TASKS_COUNT` = 0, the committed value, `TEST_CASE_II`
creates the same tasks as `TEST_CASE_I`.

`S_A` runs first. The wait of `H_Task_SA` on the `START_EVENT_MASK` post pends while `L_Task_SA`
is suspended, switches to `I_Task` and takes no stop marker. `I_Task` then resumes `L_Task_SA`
`MEASUREMENT_COUNT` times, one round per resume. The first round of `H_Task_SB` runs on the
`START_EVENT_MASK` post, so `I_Task` resumes `H_Task_SB` `MEASUREMENT_COUNT - 1` times.

## Compensation

| Probe | Series | Re-runs without the switch | May be set against |
| --- | --- | --- | --- |
| `measure_event_z_swap_overhead(SIM_Z_SWAP_PROLOGUE)` | `z_swap_overhead` | the `z_swap()` prologue, up to the `irq_unlock()` in `arch_swap()` | the `S_A` side from the `z_swap()` call in `z_pend_curr()` |
| `measure_wait_tail_no_wait_overhead()` | `wait_tail_no_wait` | the tail of `k_event_wait_safe()` on the `K_NO_WAIT` path | the `S_B` side after the `K_NO_WAIT` test |

No probe re-runs the PendSV exception or the tail of `k_event_post()`.

## Analysis

Documents, each an instruction listing from the linked `zephyr.elf`:
[doc/sa_vs_sb.html](doc/sa_vs_sb.html), both windows in execution order at an empty wait queue;
[doc/sim_vs_kernel.html](doc/sim_vs_kernel.html), the code of each probe against the kernel
code it re-runs. It also lists `measure_wait_overhead(SIM_WAIT_PROLOGUE)`, a function of
[zephyr_sim.c](zephyr_sim.c) that re-runs the `S_A` path of `k_event_wait_safe()` from the
`K_NO_WAIT` test up to the `irq_unlock()` in `arch_swap()`. `main()` does not call it, so it records no series and is not a probe of this
test case.

Expected of the base measurement: the wait queue of `test_event` is empty at the insert of
`H_Task_SA`, so the insert passes no task.

Expected of the loaded measurement: the insert of `H_Task_SA` passes the `dummy_count` dummy
tasks, `D > H_Task_SA`, and places it last. `scenario_a`, and with it `S_A - S_B`, is expected to
carry a term in `DUMMY_TASKS_COUNT`, `O(dummy_count)`. `scenario_b` is expected to carry none,
since the `K_NO_WAIT` wait inserts into no wait queue.

## Configuration

| Option | Value | Consequence special to this test case |
| --- | --- | --- |
| `CONFIG_WAITQ_SIMPLE` | y, Kconfig default | the load term of `scenario_a` rests on the insert rule of this option |
