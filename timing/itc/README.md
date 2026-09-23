# Inter-task communication

The `itc` family measures one message going from one task to another, over the Zephyr objects
listed at the end of this file. What separates the members is what the object does with the
message: hand over a pointer, or copy the bytes, and out of which storage. What separates the scenarios is
who waits for whom.

`itc` is the directory and `CONFIG_BENCHMARK_TEST_IPC_` is the prefix of the Kconfig symbols
that select these test cases. The two spellings mean the same family.

A start marker and a stop marker are reads of the cycle counter, taken by the measured tasks
themselves, and the measurement window is what lies between them.

## Terminology

| Name | Meaning |
| --- | --- |
| `T_Send*` | The sender tasks. |
| `T_Recv*` | The receiver tasks. |
| `T_Self` | The task that is sender and receiver at once. |
| `T_Arm*` | The driver tasks. One transaction per pass, and all bookkeeping. |
| `I_Task` | The control task. |
| transfer object | The kernel object the message is handed through. One kind per test case, and as many of them as the test case says. |
| storage | Where a message waits inside the transfer object while no receiver waits for it. Its form is a property of the object. |
| transaction | One send operation and the receive operation that consumes it. |
| series | One scenario recorded at one message size. |
| `A - B` | The difference between two recorded values at the same message size. `A` and `B` are either two scenarios of one test case, or one scenario of two test cases. |
| `BMTH_*` | The measurement package under `packages/benchmark_hms`, which owns the markers, the series and the result signalling. |

## Tasks

The same set in every test case, at the same priorities. `TASK_IDLE_PRIO` is the control task's
own symbol and is not the priority of the kernel idle task.

| Task | Priority macro | Priority | Scenario | Role |
| --- | --- | --- | --- | --- |
| `T_RecvUp` | `T_RECV_UP_PRIO` | 2 | `up` | waits in the receive operation, takes the stop marker |
| `T_SendDown` | `T_SEND_DOWN_PRIO` | 2 | `down` | sends, takes the start marker |
| `T_Self` | `T_SELF_PRIO` | 3 | `self` | sends and receives, takes both markers |
| `T_SendUp` | `T_SEND_UP_PRIO` | 3 | `up` | sends, takes the start marker |
| `T_RecvDown` | `T_RECV_DOWN_PRIO` | 3 | `down` | waits in the receive operation, takes the stop marker |
| `T_ArmUp`, `T_ArmDown` | `T_ARM_PRIO` | 4 | `up`, `down` | release the receiver into the receive operation, release the sender, record each transaction into its series, step the sweep |
| `I_Task` | `TASK_IDLE_PRIO` | 15 | all | creates a scenario, runs it, aborts it, reports |
| kernel idle | `K_IDLE_PRIO` | 16 | all | runs only when no test task is ready |

Lower number = higher priority. `T_RecvUp` is the higher priority task of the `up` pair,
`T_SendDown` the higher priority task of the `down` pair, so the two scenarios draw from the
same two priorities and only swap which task holds which.

Two semaphores carry the hand-offs. `test_recv_sem` puts the receiver into the receive
operation, `test_arm_sem` starts the sender. The driver releases both before the start marker.
The sender's wait on `test_arm_sem` falls inside the window in `down`, between the send
returning and the switch into the receiver. `test_event` carries the `START_*` and `DONE_*`
barriers between `I_Task` and the tasks of a scenario.

## Scenarios

| Scenario | Series | Trigger | Measured path |
| --- | --- | --- | --- |
| `self` | `self_send` | `T_Self` enters the send operation | The send into the storage of the transfer object, and the receive back out of it. One task, no switch. This is the only scenario in which the storage is used. |
| `up` | `send_to_higher_prio` | `T_ArmUp` releases `test_arm_sem` | The send, the release of the waiting receiver, the switch into it inside the send, and the rest of the receive. |
| `down` | `send_to_lower_prio` | `T_ArmDown` releases `test_arm_sem` | The same send and release without a switch, the sender blocking afterwards, and then the switch into the receiver and the rest of the receive. |

The receiver is already inside the receive operation in both cross-task scenarios, so in both
the send finds a waiter and hands the message over without entering the storage. `up - down` is
therefore the priority relation and nothing else.

A member whose send does not complete without a second task has no `self` scenario, and records
each of the other two twice, one series closing on the receiver and one on the sender's own
return.

## Measurement principle

1. The measured value is the cycle count from the start marker to the stop marker that closes
   the measurement window.
2. Sender and receiver hold the transaction, their markers, and the wait that gives up the CPU
   where the scenario needs one. Every loop, branch and bookkeeping statement lives in a task
   that is not measured.
3. The driver task sits below every measured task, so it only runs when they are all blocked.
   Nothing it does can fall inside a measurement window.
4. The receiver is put inside the receive operation before the window opens, so a send always
   finds it waiting.
5. One pass of the driver loop is one transaction. Releasing the sender hands over the CPU, and
   control comes back only once everything has blocked again and both markers are settled.
6. The message size is swept inside one build, one series per size.
7. Scenarios run one after the other, each created, driven and aborted before the next.
8. A test case differs from its neighbours in the transfer object and its operations alone:
   same tasks, same priorities, same counts, same sweep, same marker placement.

## Configuration

Shared by every member of the family, set in `prj.conf`.

| Option (`prj.conf`) | Value | Consequence for the family |
| --- | --- | --- |
| `CONFIG_MP_MAX_NUM_CPUS` | 1 | one ready queue, exactly one task runs |
| `CONFIG_SCHED_SIMPLE` | y | the ready queue is one priority sorted list (`z_priq_simple_*`) |
| `CONFIG_WAITQ_SIMPLE` | y | every transfer object and every semaphore owns one priority sorted wait queue; an insert walks from the head, and with one waiter it stops at the first node |
| `CONFIG_TIMESLICING` | n | no involuntary rotation, tasks of equal priority change only on an explicit call |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | priorities 0..15, all preemptible, the kernel idle task sits at 16 |
| `CONFIG_POLL` | n | the poll branches of the send operations are compiled out of the measured path |
| `CONFIG_TRACING`, `CONFIG_OBJ_CORE` | n | the `SYS_PORT_TRACING_*` macros inside the measured operations expand to nothing |
| `CONFIG_FORCE_NO_ASSERT` | y | the `__ASSERT` statements inside them expand to nothing |
| `CONFIG_DEBUG_OPTIMIZATIONS` | y | `-Og`, identical for every test case |

Bookkeeping, identical in every test case. `MEASUREMENT_COUNT` = 50000 transactions per series,
and one series per message size: the sweep runs over `step` = 0 to `MESSAGE_SIZE_STEPS - 1`,
`MESSAGE_SIZE_STEPS` = 9, the size doubling from `MESSAGE_SIZE_MIN` = 4 B to
`MESSAGE_SIZE_MAX` = 1024 B. The step loop lives in `T_Self` and the driver tasks, so no value
in any source selects a point of the sweep. Scenarios run in the order `self`, `up`, `down`,
`I_Task` aborting the tasks of one and resetting both semaphores before it creates the next
set. `BMTH_mwindow_open()` and `BMTH_mwindow_close()`, called by the driver task outside the
measured interval, gate `BMTH_mseries_iterate()` and decide whether an iteration is recorded. A series
whose two markers are taken in two different tasks is initialised as
`BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS`, one whose markers are taken in
a single function as `BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS`. Before the
first series, `main()` fills the message buffers, turns the low power cache controller off
(`SYSCON->LPCAC_CTRL`, so no instruction fetch is served from cache inside a measurement
window), disables the SysTick (so no tick interrupt falls into one), and validates the DWT
cycle counter the markers read with `BMTH_check_read_validity()`. `I_Task` sums
`values_outlier_count` over every series and reports through `BMTH_signalize_mseries_stop()`,
which fails if any series counted an outlier. Nothing is subtracted from a recorded value.

## Family members

| Directory | Readme | What separates it |
| --- | --- | --- |
| `fifo` | [fifo/README.md](fifo/README.md) | `k_fifo`, a list entered at the tail. |
| `lifo` | [lifo/README.md](lifo/README.md) | `k_lifo`, the same list entered at the head. |
| `stack` | [stack/README.md](stack/README.md) | `k_stack`, an array instead of a list. |
| `msgq` | [msgq/README.md](msgq/README.md) | `k_msgq`, copy by value, the size fixed at creation. |
| `pipe` | [pipe/README.md](pipe/README.md) | `k_pipe`, copy by value, the size an argument per call. Two test cases, with a ring and without one. |
| `mailbox` | [mailbox/README.md](mailbox/README.md) | `k_mbox`, no copy in the send, the receiver takes the one copy in `k_mbox_get()`. Two test cases, asynchronous and synchronous send. |
