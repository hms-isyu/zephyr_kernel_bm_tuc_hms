# irt, interrupt response time via a self-induced interrupt

Set `CONFIG_BENCHMARK_TEST_IRT=y` in [prj.conf](../../prj.conf), then set `TEST_CASE` in
`src/irt/benchmark_testcases.h` to one of `TEST_CASE_I..TEST_CASE_VIII`, and rebuild. One test
case per build. The source is `src/irt/time_interrupt_response.c`. Build and flash as described
in the [general readme](../../README.md).

This test times the path from the call that pends an interrupt to the point where the task
readied by the interrupt handler runs again, or, when the handler readies no task, to the first
statement of the handler. The eight test cases are the grid of four ways for the handler to
ready the task and two ways for the interrupt to reach the handler. Two test cases that share
one of the two choices differ by the other alone.

## Terminology

| Term | Meaning |
| --- | --- |
| start marker | `BMTH_GET_START_CNT()`, a read of the `DWT->CYCCNT` cycle counter |
| stop marker | `BMTH_GET_STOP_CNT()`, a read of the same counter |
| measurement window | the code executed between the start marker and the stop marker of one iteration |
| outlier | an output value of a series that differs from the running `values_average` of that series, truncated to an integer; `BMTH_mseries_iterate()` counts it in `values_outlier_count`. The first output value of a series is never an outlier. |
| release | the call in `test_isr` that makes `A_Task_SA` ready, selected by `SA_RELEASE`: `RESUME` (`k_thread_resume()`), `SEM` (`k_sem_give()`), `EVENT` (`k_event_post()`). `SA_RELEASE` = `NONE` selects no release. |
| release wait | the call in which `A_Task_SA` waits for the release: `k_thread_suspend()` for `RESUME`, `k_sem_take()` for `SEM`, `k_event_wait_safe()` for `EVENT` |
| dispatch | how the exception on `TEST_IRQ_LINE` reaches `test_isr`, the value of `TEST_IRQ_DISPATCH`: `TABLE`, through `_isr_wrapper` and the software ISR table (`IRQ_CONNECT()`), or `DIRECT`, `test_isr` placed in the vector table (`IRQ_DIRECT_CONNECT()`, `ISR_DIRECT_DECLARE()`) |

A scenario is named by the series it records into: `scenario_a` or `scenario_b`.

## Participants

| Task | Priority macro | Present when | Role |
| --- | --- | --- | --- |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` | `scenario_a` runs | takes the start marker of `scenario_a`, calls `TRIGGER_TEST_IRQ()` |
| `A_Task_SA` | `TASK_SA_APERIODIC_PRIO` | `scenario_a` runs and `SA_RELEASE` is not `NONE` | waits in the release wait, takes the stop marker of `scenario_a` when it returns |
| `Task_SB` | `TASK_SB_PRIO` | `scenario_b` runs | takes both markers of `scenario_b` around `TRIGGER_TEST_IRQ()` |
| `I_Task` | `TASK_IDLE_PRIO` | always | control task: creates and aborts the tasks of each scenario, enables and disables `TEST_IRQ_LINE`, reports |

`TASK_SA_APERIODIC_PRIO < TASK_SA_LOW_PRIO == TASK_SB_PRIO < TASK_IDLE_PRIO`, a lower value being
a higher priority. `TASK_IDLE_PRIO` is the symbol of the control task, not the priority of the
kernel idle thread.

| Name | What it is |
| --- | --- |
| `TRIGGER_TEST_IRQ()` | `NVIC_SetPendingIRQ(TEST_IRQ_LINE)`, the call that pends the interrupt in both scenarios |
| `TEST_IRQ_LINE` | `Reserved166_IRQn`, at NVIC priority `TEST_IRQ_PRIO` = 0, in every test case |
| `test_isr` | the handler of `TEST_IRQ_LINE`; its body, `TEST_ISR_BODY()`, is the release, or the stop marker of `scenario_a` when `SA_RELEASE` is `NONE` |
| `test_sem` | the object of the `SEM` release |
| `test_release_event` | the object of the `EVENT` release, carrying `SCENARIO_A_EVENT_MASK` |
| `test_event` | the `START_EVENT_MASK` and `SIGNALIZE_DONE_EVENT_MASK` barrier between `I_Task` and the scenario tasks |

## Measurement

In both scenarios `TRIGGER_TEST_IRQ()` starts the measurement by pending `TEST_IRQ_LINE`.

| Scenario | Trigger | Measurement window | What the window holds |
| --- | --- | --- | --- |
| `scenario_a`, `SA_RELEASE` is `RESUME`, `SEM` or `EVENT` | `L_Task_SA` calls `TRIGGER_TEST_IRQ()`, `TEST_IRQ_LINE` enabled | start marker in `L_Task_SA` before `TRIGGER_TEST_IRQ()`, stop marker in `A_Task_SA` after its release wait returns | `TRIGGER_TEST_IRQ()`, exception entry, the dispatch, the release, `z_arm_exc_exit()`, the switch into `A_Task_SA`, the return from the release wait |
| `scenario_a`, `SA_RELEASE` is `NONE` | as above | start marker in `L_Task_SA` before `TRIGGER_TEST_IRQ()`, stop marker as the body of `test_isr` | `TRIGGER_TEST_IRQ()`, exception entry, the dispatch |
| `scenario_b` | `Task_SB` calls `TRIGGER_TEST_IRQ()`, `TEST_IRQ_LINE` disabled | both markers in `Task_SB`, immediately before and after `TRIGGER_TEST_IRQ()` | `TRIGGER_TEST_IRQ()` |

The output value is `stop marker - start marker - values_static_overhead`. In `scenario_a`, both
`L_Task_SA`, after `TRIGGER_TEST_IRQ()` returns, and `A_Task_SA`, after its stop marker, call
`BMTH_mseries_iterate()` and then `BMTH_mwindow_close()`; the first of the two records the output
value, and the call of the other records nothing. With `SA_RELEASE` = `NONE`, `L_Task_SA`
records. With a release, `A_Task_SA` records under both dispatches: both end the ISR in
`z_arm_exc_exit()`, under `DIRECT` because the `ISR_DIRECT_DECLARE()` body of `test_isr` returns
`TEST_ISR_RESCHEDULE` = 1. `z_arm_exc_exit()` pends the switch into `A_Task_SA`, which the
release has made ready and `TASK_SA_APERIODIC_PRIO < TASK_SA_LOW_PRIO` ranks first, and the switch
takes place before `L_Task_SA` resumes.

`scenario_a - scenario_b` is the window of `scenario_a` without `TRIGGER_TEST_IRQ()`. This rests
on the assumption that `TRIGGER_TEST_IRQ()` costs the same with `TEST_IRQ_LINE` enabled and
disabled: `NVIC_SetPendingIRQ()` (CMSIS `core_cm33.h`) is one store to `NVIC->ISPR`, with no
branch on the enable state of the line. The difference does not isolate `TRIGGER_TEST_IRQ()`
itself.

| Test case | `SA_RELEASE` | `TEST_IRQ_DISPATCH` |
| --- | --- | --- |
| `TEST_CASE_I` | `RESUME` | `TABLE` |
| `TEST_CASE_II` | `SEM` | `TABLE` |
| `TEST_CASE_III` | `EVENT` | `TABLE` |
| `TEST_CASE_IV` | `NONE` | `TABLE` |
| `TEST_CASE_V` | `RESUME` | `DIRECT` |
| `TEST_CASE_VI` | `SEM` | `DIRECT` |
| `TEST_CASE_VII` | `EVENT` | `DIRECT` |
| `TEST_CASE_VIII` | `NONE` | `DIRECT` |

No test case adds work to the window beyond its release and its dispatch.

`benchmark_testcases.h` derives `SA_RELEASE` and `TEST_IRQ_DISPATCH` from `TEST_CASE` as in the
table above, and defines `TEST_CASE` as `TEST_CASE_VIII` when it is not already defined. No
`CONFIG_BENCHMARK_TEST_*` symbol other than `CONFIG_BENCHMARK_TEST_IRT` reaches this test.

`I_Task` enables `TEST_IRQ_LINE` and drives `scenario_a` to `MEASUREMENT_COUNT` iterations, aborts
`L_Task_SA` and `A_Task_SA`, then creates `Task_SB` and drives `scenario_b` to
`MEASUREMENT_COUNT` iterations.

## Compensation

| Task | Series | Re-runs, without the exception |
| --- | --- | --- |
| `Task_SB` | `scenario_b` | `TRIGGER_TEST_IRQ()`: before creating `Task_SB`, `I_Task` disables `TEST_IRQ_LINE` with `irq_disable()` and clears its pending state with `NVIC_ClearPendingIRQ()`; `Task_SB` clears the pending state again after each iteration |

## Analysis

Each row subtracts `scenario_a - scenario_b` of one test case from `scenario_a - scenario_b` of
another.

| Minuend | Subtrahend | Difference | Expected |
| --- | --- | --- | --- |
| `TEST_CASE_I`, `II` or `III` | `TEST_CASE_IV` | the release, `z_arm_exc_exit()`, the switch into `A_Task_SA` and the return from the release wait, under `TABLE` | `> 0` |
| `TEST_CASE_V`, `VI` or `VII` | `TEST_CASE_VIII` | the same, under `DIRECT` | `> 0` |
| `TEST_CASE_I` | `TEST_CASE_II` | `k_thread_resume()` with the return from `k_thread_suspend()`, against `k_sem_give()` with the return from `k_sem_take()`, under `TABLE`; likewise for any two of `TEST_CASE_I..III`, and of `TEST_CASE_V..VII` under `DIRECT` | none stated |
| `TEST_CASE_I`, `II`, `III`, `IV` | `TEST_CASE_V`, `VI`, `VII`, `VIII` respectively | the `TABLE` dispatch against the `DIRECT` dispatch at the same release: entry up to `test_isr` for (`IV`, `VIII`), entry and the path to `z_arm_exc_exit()` for the other three pairs | `> 0`: `_isr_wrapper` reads `IPSR`, indexes `_sw_isr_table` and calls `test_isr` through the table entry, steps the `ISR_DIRECT_DECLARE()` wrapper does not perform |

## Configuration

Options of [prj.conf](../../prj.conf) the measurement of this test depends on:

| Option | Value | Effect on the measurement |
| --- | --- | --- |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | sets `CONFIG_PREEMPT_ENABLED`, without which `z_arm_exc_exit()` pends no switch |
| `CONFIG_EVENTS` | y | provides `test_event` and `test_release_event` |

Bookkeeping, identical in every test case. `MEASUREMENT_COUNT` = 50000 iterations per series.
`scenario_a` is initialised with `BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS`
(its markers sit in two tasks, or in a task and `test_isr`), `scenario_b` with
`BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS` (both markers sit in `Task_SB`);
each mode sets the `values_static_overhead` of its series. Before either scenario runs, `main()`
does the following:

| Step in `main()` | Effect on the measurement |
| --- | --- |
| disables and clears the low power cache controller (`SYSCON->LPCAC_CTRL`) | no access in a window is served from that cache |
| `BMTH_disable_sys_tick()` | no tick interrupt enters a window |
| `BMTH_check_read_validity()`, which fails unless a start marker followed by a stop marker yields the same count `MEASUREMENT_COUNT` times in a row | a failure calls `BMTH_signalize_jitter_detected()` |

`I_Task` reports through `BMTH_signalize_mseries_stop()`, with `false` when `scenario_a` or
`scenario_b` counted an outlier, `true` otherwise.
