# Task Switchting time

General readme for the `task_switch` test family. Terms defined here apply to every
sub-directory of this family.

## Terminology

| Term | Symbol | Meaning |
| --- | --- | --- |
| Low priority task | `L`, `L_Task` | The principal low priority task of the base measurement. |
| High priority task | `H`, `H_Task` | The principal high priority task of the base measurement. |
| Dummy tasks | `D`, Dummies | Tasks that add worst case load to the base measurement. Their priority may vary depending on the measurement. |
| Workload | - | The work that is done inside the measurement. |
| Measurement window | - | The relevant code path that is measured, the measurement path. |
| Start/Stop markers | - | The start or stop of the time measurement. |

## Scenarios

| Scenario | Trigger | Measured path |
| --- | --- | --- |
| A | Transition (preemption or blocking) | The path that leads to a context and task switch, usually `L -> H`. |
| B | Activation API |  |

## Measurement principle

1. The measured quantity is always the time spent inside the measurement window, measured in cycles.
2. The base measurement runs the smallest workload possible inside that window.
3. Dummies are added on top of the base measurement to place relevant load on the
   workload.
4. Compared cases differ in the way that workload is added to the base measruement.
5. (optional) Diassemble and simulate epi- and prologues that may cause confounding. Measure their duration.

## Configuration

Shared by every member of the family, set in `prj.conf`. A sub-directory repeats an option
only where its value or its consequence is special to that measurement.

| Option (`prj.conf`) | Value | Consequence for the queues |
| --- | --- | --- |
| `CONFIG_MP_MAX_NUM_CPUS` | 1 | one ready queue, exactly one thread runs |
| `CONFIG_SCHED_MULTIQ` | y | ready queue is one FIFO list per priority plus a bitmask (`z_priq_mq_*`); add, remove and "pick best" are O(1) and independent of occupancy |
| `CONFIG_WAITQ_SIMPLE` | y | every `k_event` and `k_sem` owns one priority-sorted list (`z_priq_simple_*`); an insert walks from the head until it meets a thread the inserted one outranks |
| `CONFIG_TIMESLICING` | n | no involuntary rotation inside a band, threads of equal priority change only on an explicit call |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | priorities 0..15, all preemptible; the kernel idle thread sits at `K_IDLE_PRIO` = 16, so every test task > idle |

Lower number = higher priority. *Band* = the ready-queue list of one priority. A thread that
calls `k_thread_suspend()` leaves the ready queue and joins no wait queue, suspension is a
thread state, not a queue.

Common bookkeeping: `MEASUREMENT_COUNT` = 50000 iterations per series;
`BMTH_mwindow_open()` / `BMTH_mwindow_close()` gate `BMTH_mseries_iterate()`, so an iteration
is recorded only while the window is open and any thread running inside the window can
invalidate it by closing it. `scenario_a` is initialised as a cross-function read window,
`scenario_b` as an inside-function one, matching where their markers are taken. `I_Task`
reports through `BMTH_signalize_mseries_stop()` and fails if a series counted an outlier.

## Family members

| Directory | Readme | Subject |
| --- | --- | --- |
| `task_change` | [task_change/README.md](task_change/README.md) | Same priority switch handed on with `k_yield()`, no preemption decision. |
| `task_preempt` | [task_preempt/README.md](task_preempt/README.md) | Preempting switch triggered by `k_thread_resume()`, ready queue only, no wait queue in the path. |
| `sem_preempt` | [sem_preempt/README.md](sem_preempt/README.md) | Preempting switch triggered by `k_sem_give()`, releases the head of the wait queue only. |
| `event_preempt` | [event_preempt/README.md](event_preempt/README.md) | Preempting switch triggered by `k_event_post()`, walks the entire wait queue. |
| `event_block` | [event_block/README.md](event_block/README.md) | Blocking switch, `k_event_wait_safe()` pends the caller itself and gives up the CPU. |
