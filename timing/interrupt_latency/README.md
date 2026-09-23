# interrupt_latency, interrupt response via a self-induced interrupt

Set `CONFIG_BENCHMARK_TEST_INTERRUPT_RESPONSE_TIME=y` in `prj.conf` (one test case per build),
then set `TEST_CASE` in `benchmark_testcases.h` to `TEST_CASE_I` through `TEST_CASE_VIII` and
rebuild. `TEST_CASE` is the only symbol edited: `benchmark_testcases.h` derives `SA_RELEASE` and
`TEST_IRQ_DISPATCH` from its value. The source is
`timing/interrupt_latency/time_interrupt_response.c`.

This test times the path from the call that pends an interrupt to the point where its effect is
visible to a task: the first instruction the task released by the ISR runs after it wakes, or,
when no task is released, the first instruction inside the ISR itself. Eight test cases cross
two discrete choices along that path: the release the ISR uses to hand off, and the dispatch
that reaches the ISR. `TEST_CASE_I` through `TEST_CASE_IV` hold the dispatch at `TABLE` and sweep
the release; `TEST_CASE_V` through `TEST_CASE_VIII` repeat the same sweep at `DIRECT`. The pair
(`TEST_CASE_I`, `TEST_CASE_V`), and likewise (II, VI), (III, VII), (IV, VIII), holds the release
fixed and isolates the dispatch alone; `TEST_CASE_I` against `TEST_CASE_II` holds the dispatch
fixed and isolates the release alone.

## Terminology

| Term | Meaning |
| --- | --- |
| S_A | The scenario recorded in series `scenario_a`, and the cycle count it records: the round trip from `TRIGGER_TEST_IRQ()` to the task the ISR releases, or to the ISR itself when there is no release. |
| S_B | The scenario recorded in series `scenario_b`, and the cycle count it records: the same call to `TRIGGER_TEST_IRQ()` alone, with the interrupt line disabled. |
| release | The way the ISR hands off to the released task: `RESUME` (`k_thread_resume()`), `SEM` (`k_sem_give()`), `EVENT` (`k_event_post()`), or `NONE` (no hand-off at all). Its value, `SA_RELEASE` in `benchmark_testcases.h`, follows from `TEST_CASE`. |
| dispatch | The way the interrupt line reaches its handler: through the software ISR table (`TABLE`, `IRQ_CONNECT()`), or directly, the handler placed in the vector table (`DIRECT`, `IRQ_DIRECT_CONNECT()`). Its value, `TEST_IRQ_DISPATCH` in `benchmark_testcases.h`, follows from `TEST_CASE`. |
| probe | The task that repeats the trigger of the measured event without letting it run: `Task_SB`. |

## Participants

| Task | Priority macro | Priority | Present when | Role |
| --- | --- | --- | --- | --- |
| `L_Task_SA` | `TASK_SA_LOW_PRIO` | 3 | always | opens the S_A window, calls `TRIGGER_TEST_IRQ()`, calls `BMTH_mwindow_close()` for it when `SA_RELEASE` is `NONE` or `TEST_IRQ_DISPATCH` is `DIRECT` |
| `A_Task_SA` | `TASK_SA_APERIODIC_PRIO` | 2 | `SA_RELEASE` is `RESUME`, `SEM` or `EVENT` | waits on the release object, takes the stop marker of S_A on wake, calls `BMTH_mwindow_close()` for it when `TEST_IRQ_DISPATCH` is `TABLE` |
| `Task_SB` | `TASK_SB_PRIO` | 3 | always, after `L_Task_SA` and `A_Task_SA` are aborted | opens and closes the S_B window, calls `TRIGGER_TEST_IRQ()` with the line disabled |
| `I_Task` | `TASK_IDLE_PRIO` | 15 | always | control task: creates the tasks of each scenario, arms and disarms the interrupt line, aborts the tasks, reports |

`TASK_SA_APERIODIC_PRIO` < `TASK_SA_LOW_PRIO`: `A_Task_SA` is the higher priority of the two S_A
tasks (lower number, higher priority), so a release always preempts `L_Task_SA` into
`A_Task_SA`. `TASK_SB_PRIO == TASK_SA_LOW_PRIO`.

Measured operations and objects, named as the source names them:

| Name | What it is |
| --- | --- |
| `TRIGGER_TEST_IRQ()` | `NVIC_SetPendingIRQ(TEST_IRQ_LINE)`, the pend API both scenarios call |
| `TEST_IRQ_LINE` | `Reserved166_IRQn`, the interrupt line pended in every test case |
| `TEST_IRQ_PRIO` | `0`, the NVIC priority of that line in every test case |
| `test_isr` | the handler: a plain function called through the ISR table when `TEST_IRQ_DISPATCH` is `TABLE`, an `ISR_DIRECT_DECLARE()` body when it is `DIRECT` |
| `test_sem` | `struct k_sem`, the release object when `SA_RELEASE` is `SEM` |
| `test_release_event` | `struct k_event`, the release object when `SA_RELEASE` is `EVENT`, carrying `SCENARIO_A_EVENT_MASK` |
| `test_event` | `struct k_event`, the `START_EVENT_MASK` and `SIGNALIZE_DONE_EVENT_MASK` barrier between `I_Task` and the scenario tasks |
| `scenario_a`, `scenario_b` | `BMTH_measurement_series_t`, one series per scenario |

## Measurement

`TRIGGER_TEST_IRQ()` is the call that starts the measured event in both scenarios: `L_Task_SA`
calls it for S_A, `Task_SB` calls it for S_B.

| Scenario | Measurement window | What the window holds |
| --- | --- | --- |
| S_A | start in `L_Task_SA` before `TRIGGER_TEST_IRQ()`; stop in `A_Task_SA` after its release wait returns, or inside `test_isr` itself when `SA_RELEASE` is `NONE`. `BMTH_mwindow_close()`, called next, does not move this stop marker. | exception entry, the dispatch, the release call, and, unless `SA_RELEASE` is `NONE`, the switch into `A_Task_SA` and its return from the release wait |
| S_B | both markers in `Task_SB`, start before `TRIGGER_TEST_IRQ()`, stop immediately after | the pend API call alone |

S_A - S_B isolates everything the S_A window adds beyond the pend API call, since S_B holds
nothing else. It does not isolate the pend API call itself, which both scenarios execute alike.

| Test case | `SA_RELEASE` | `TEST_IRQ_DISPATCH` |
| --- | --- | --- |
| TEST_CASE_I | `RESUME` | `TABLE` |
| TEST_CASE_II | `SEM` | `TABLE` |
| TEST_CASE_III | `EVENT` | `TABLE` |
| TEST_CASE_IV | `NONE` | `TABLE` |
| TEST_CASE_V | `RESUME` | `DIRECT` |
| TEST_CASE_VI | `SEM` | `DIRECT` |
| TEST_CASE_VII | `EVENT` | `DIRECT` |
| TEST_CASE_VIII | `NONE` | `DIRECT` |

None of the eight adds a workload beyond the release and dispatch choice. `TEST_CASE` in
`benchmark_testcases.h` selects the point in this set; no
`CONFIG_BENCHMARK_TEST_*` symbol reaches into this family beyond the one that links the
directory, and the source defaults to `TEST_CASE_VIII` when `TEST_CASE` is left unset.

`I_Task` drives S_A to `MEASUREMENT_COUNT` iterations, aborts the S_A tasks, then creates
`Task_SB` and drives S_B to `MEASUREMENT_COUNT` iterations. S_A always runs before S_B.

## Compensation

| Probe | Series | Chain re-run without the measured event |
| --- | --- | --- |
| `Task_SB` | `scenario_b` | `TRIGGER_TEST_IRQ()` alone: `TEST_IRQ_LINE` is disabled with `irq_disable()` and its pending state cleared with `NVIC_ClearPendingIRQ()` before `Task_SB` is created, so the pend API executes and no exception is taken |

## Analysis

Expected of every test case: S_A > S_B; the S_A - S_B difference is what the pend API alone does
not spend.

Expected of the dispatch-isolating pairs: the S_A - S_B of the `TABLE` side of each pair is
expected larger than that of its `DIRECT` side, by the vector read and the `_sw_isr_table` index
that `_isr_wrapper` performs before reaching `test_isr` and that `IRQ_DIRECT_CONNECT()` skips.

Expected of the release-isolating comparisons within `TABLE`, and within `DIRECT`: the S_A - S_B
of `RESUME`, `SEM` and `EVENT` isolates the release call and the switch into `A_Task_SA`; `NONE`
isolates neither, its S_A window closing inside `test_isr` itself, before any switch.

## Configuration

Set in `prj.conf`, shared with every other test case in the repository.

| Option (`prj.conf`) | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_MP_MAX_NUM_CPUS` | 1 | one ready queue: `A_Task_SA`, `L_Task_SA` and `Task_SB` never run on separate cores, so the switch inside S_A is always the one measured |
| `CONFIG_TIMESLICING` | n | no involuntary rotation; `L_Task_SA` and `Task_SB` share priority 3 and never run at the same time, so this has no case to apply to here |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | priorities 0..15, all preemptible; `TASK_IDLE_PRIO` = 15 sits at the bottom of that range |
| `CONFIG_EVENTS` | y | required for `test_event` and, when `SA_RELEASE` is `EVENT`, `test_release_event` |
| `CONFIG_FORCE_NO_ASSERT` | y | the `__ASSERT` statements inside `test_isr`, the release calls and the kernel exception exit expand to nothing |
| `CONFIG_DEBUG_OPTIMIZATIONS` | y | `-Og`, identical for every test case |

Bookkeeping, identical in every test case. `MEASUREMENT_COUNT` = 50000 iterations per series.
`scenario_a` is initialised `BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS` (its
two markers sit in two different tasks, or a task and `test_isr`), `scenario_b` as
`BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS` (both markers sit in `Task_SB`).
Before either scenario runs, `main()` turns the low power cache controller off
(`SYSCON->LPCAC_CTRL`, so no instruction fetch is served from cache inside a measurement window),
disables the SysTick, and validates the DWT cycle counter the markers read with
`BMTH_check_read_validity()`. `I_Task` reports through `BMTH_signalize_mseries_stop()`, which
fails if `scenario_a` or `scenario_b` counted an outlier.
