# irt, interrupt response time via a self-induced interrupt

The test cases under `src/irt` measure how long a task takes to respond to a self-induced
interrupt, as the cycle count from the interrupt being pended to the point its effect becomes
visible: in the task the handler releases, or, absent a release, in the handler itself. They
compare that response along two axes: the dispatch by which the interrupt reaches its handler,
and the release the handler performs once there.

## Terminology

| Term | Meaning |
| --- | --- |
| release | the operation the handler performs to make a waiting task ready |
| dispatch | the path by which a pended interrupt reaches its handler |
| control task | the task that creates and removes the tasks of each scenario |

## Scenarios

| Scenario | Trigger | Measured path |
| --- | --- | --- |
| `scenario_a` | `TRIGGER_TEST_IRQ()`, `TEST_IRQ_LINE` enabled | the start marker precedes the trigger; the interrupt runs through the dispatch and the release; where the release makes a task ready, the stop marker sits in that task once its wait returns; where there is no release, the stop marker sits in the handler itself |
| `scenario_b` | `TRIGGER_TEST_IRQ()` alone, `TEST_IRQ_LINE` disabled | both markers sit immediately around `TRIGGER_TEST_IRQ()`; the handler does not run |

## Measurement principle

1. The output value is the cycle count from the start marker to the stop marker.
2. One test case builds one combination of release and dispatch.
3. The control task has a lower priority than every measured task, so it runs only while they
   are blocked, and none of its statements falls inside a window.
4. The scenarios run one after the other; the tasks of one are removed before the tasks of the other are created.

## Configuration

Shared by every test case.

| Setting | Value | Set in | Meaning |
| --- | --- | --- | --- |
| `CONFIG_BENCHMARK_TEST_IRT` | y | `prj.conf` | selects this family; one test case per build |
| `TEST_CASE` | one of `TEST_CASE_I..TEST_CASE_VIII` | `benchmark_testcases.h` | selects the combination of release and dispatch the build measures |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | `prj.conf` | priorities `0..15`, all preemptible; a release is followed by an immediate switch into the task it makes ready |
| `CONFIG_EVENTS` | y | `prj.conf` | provides the kernel object the event release uses |
| priority convention | lower value is higher priority | `benchmark_testcases.h` | `TASK_IDLE_PRIO`, the control task's priority, is a higher number than every other task priority the family defines |

## Family members

| Test case | Release | Dispatch |
| --- | --- | --- |
| `TEST_CASE_I` | `SA_RELEASE_RESUME` | `IRQ_DISPATCH_TABLE` |
| `TEST_CASE_II` | `SA_RELEASE_SEM` | `IRQ_DISPATCH_TABLE` |
| `TEST_CASE_III` | `SA_RELEASE_EVENT` | `IRQ_DISPATCH_TABLE` |
| `TEST_CASE_IV` | `SA_RELEASE_NONE` | `IRQ_DISPATCH_TABLE` |
| `TEST_CASE_V` | `SA_RELEASE_RESUME` | `IRQ_DISPATCH_DIRECT` |
| `TEST_CASE_VI` | `SA_RELEASE_SEM` | `IRQ_DISPATCH_DIRECT` |
| `TEST_CASE_VII` | `SA_RELEASE_EVENT` | `IRQ_DISPATCH_DIRECT` |
| `TEST_CASE_VIII` | `SA_RELEASE_NONE` | `IRQ_DISPATCH_DIRECT` |
