# Inter-task communication

The test cases under `src/itc` time one message passing from one task to another through a
Zephyr kernel object. The members differ in the transfer object: what it passes (an address or
the bytes of the message) and where it keeps a message that no receiver waits for. The
scenarios differ in who waits for whom.

`CONFIG_BENCHMARK_TEST_ITC_` is the prefix of the Kconfig symbols that select these test cases.
The start marker is `BMTH_RESET_COUNTER()` followed by `BMTH_GET_START_CNT()`, the stop marker
is `BMTH_GET_STOP_CNT()`. Both read the DWT cycle counter and are taken by the measured tasks.

## Terminology

| Term | Meaning |
| --- | --- |
| sender | The task that calls the send operation and takes the start marker: `T_SendUp`, `T_SendDown`, `T_Self`. |
| receiver | The task that calls the receive operation: `T_RecvUp`, `T_RecvDown`, `T_Self`. |
| driver | `T_ArmUp`, `T_ArmDown`. Gives the two semaphores that start receiver and sender, records each transaction into its series, and advances the message size. |
| transfer object | The kernel object the message passes through, one kind per member. Its send operation takes the message in, its receive operation hands it out. |
| storage | Where the transfer object keeps a message while no receiver waits for it. |
| waiting receiver | A receiver pended inside the receive operation on the transfer object. |
| transaction | One send operation and the receive operation that takes its message. |
| series | One `BMTH_measurement_series_t`: one scenario at one `step`. `self_send[step]` is the `self` series at `step`. |
| member, test case | A member is a directory under `src/itc`. A test case is one `CONFIG_BENCHMARK_TEST_ITC_*` symbol and the source file it builds, named by that file without `.c`. A member holds one or two test cases. |
| `A - B` | The difference of two recorded values at the same `step`: two scenarios or series of one test case, or one series of two test cases. |

`X > Y` between two tasks means `X` has the higher priority (the lower number).

## Tasks

The same tasks and priorities in every test case. `T_Self` exists only in the test cases that
have a `self` scenario. `TASK_IDLE_PRIO` is the symbol of `I_Task` and is not the priority of
the kernel idle task.

| Task | Priority macro | Priority | Scenario | Role |
| --- | --- | --- | --- | --- |
| `T_RecvUp` | `T_RECV_UP_PRIO` | 2 | `up` | receiver, takes the stop marker |
| `T_SendDown` | `T_SEND_DOWN_PRIO` | 2 | `down` | sender, takes the start marker |
| `T_Self` | `T_SELF_PRIO` | 3 | `self` | sender and receiver, takes both markers, runs its own loops and series calls outside its window |
| `T_SendUp` | `T_SEND_UP_PRIO` | 3 | `up` | sender, takes the start marker |
| `T_RecvDown` | `T_RECV_DOWN_PRIO` | 3 | `down` | receiver, takes the stop marker |
| `T_ArmUp`, `T_ArmDown` | `T_ARM_PRIO` | 4 | `up`, `down` | driver |
| `I_Task` | `TASK_IDLE_PRIO` | 15 | all | creates the tasks of a scenario, starts it, aborts it, reports |
| kernel idle | `K_IDLE_PRIO` | 16 | all | runs only when no test task is ready |

`T_RecvUp > T_SendUp` and `T_SendDown > T_RecvDown`: the two cross-task scenarios use the same
two priorities and swap which role holds the higher one.

Per transaction the driver gives `test_recv_sem`, on which the receiver waits before it enters
the receive operation, then calls `BMTH_mwindow_open()` and gives `test_arm_sem`, on which the
sender waits before its start marker. `test_event` carries the `START_*` and `DONE_*` barriers
between `I_Task` and the tasks of a scenario.

## Scenarios

| Scenario | Series | Trigger | Start marker | Stop marker | Measured path |
| --- | --- | --- | --- | --- | --- |
| `self` | `self_send` | `T_Self` reaches its start marker | `T_Self` | `T_Self`, after the receive operation returns | The send operation into the storage and the receive operation out of it. One task, no switch. The only scenario that uses the storage. |
| `up` | `send_to_higher_prio` | the driver gives `test_arm_sem` | `T_SendUp` | `T_RecvUp`, after the receive operation returns | The send operation up to the point where it readies the waiting receiver, the switch into the receiver out of the send operation, the rest of the receive operation. |
| `down` | `send_to_lower_prio` | the driver gives `test_arm_sem` | `T_SendDown` | `T_RecvDown`, after the receive operation returns | The send operation up to the point where it readies the waiting receiver, the rest of the send operation and its return, the sender's `k_sem_take()` on `test_arm_sem` up to the point where it pends, the switch into the receiver, the rest of the receive operation. |

The measured paths of `up` and `down` hold for a send operation that returns once it has readied
the receiver, which is every test case except `mailbox_sync`.

`pipe_sync` and `mailbox_sync` have no `self` scenario. They record each of `up` and `down` in
two series that share the start marker: `send_to_higher_prio` and `send_to_lower_prio` stop in
the receiver after the receive operation returns, `rendezvous_to_higher_prio` and
`rendezvous_to_lower_prio` stop in the sender after the send operation returns.

In `up` and `down` the receiver is a waiting receiver when the send operation starts. Every
send operation looks for a waiting receiver before it touches its storage, finds one, and hands
the message to it without entering the storage.

`down - up`: both windows hold the same send operation up to readying the receiver, exactly one
switch, and the same rest of the receive operation. `down` also holds the rest and the return of
the send operation and the sender's `k_sem_take()` on `test_arm_sem` up to its pend. The switch
leaves the sender from inside the send operation in `up` and from `k_sem_take()` in `down`.
`down - up` is these two differences and holds no work of the storage.

`self - up`: `self` holds the storage path of both operations and no switch, `up` holds the
waiting receiver path and one switch. `self - up` therefore never isolates the storage.

## Measurement principle

1. The measured value is the cycle count from the start marker to the stop marker.
2. A measurement window holds the transaction and, where the scenario needs it, the wait in
   which a measured task gives up the CPU. Window bookkeeping, loops and the change of message
   size run outside every window: in the driver for the cross-task scenarios, around the window
   in the measured task for the single-task scenario.
3. The driver has a lower priority than every measured task, so it runs only while all of them
   are blocked, and none of its statements falls inside a window.
4. The receiver becomes a waiting receiver before the driver starts the sender, so every
   cross-task send operation finds it.
5. One pass of the driver loop is one transaction. Giving the sender its start hands over the
   CPU, and the driver runs again only once both measured tasks have blocked and both markers
   are taken.
6. The message size takes every value of its domain inside one build, one series per scenario
   and message size.
7. Scenarios run one after the other, each created, driven and aborted before the next.
8. A test case differs from its neighbours in the transfer object, its operations and the setup
   the object needs outside the windows: same tasks, priorities, counts, message sizes and
   marker placement.

## Configuration

Shared by every test case. Options not set in `prj.conf` keep their Kconfig default.

| Setting | Value | Set in | Consequence for the family |
| --- | --- | --- | --- |
| `CONFIG_BENCHMARK_TEST_ITC_*` | y for one symbol of the Kconfig choice `BENCHMARK_TEST` | `prj.conf` | one test case per build |
| `CONFIG_MP_MAX_NUM_CPUS` | 1 | `prj.conf` | one CPU, exactly one task runs |
| `CONFIG_TIMESLICING` | n | `prj.conf` | tasks of equal priority change only on an explicit call |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | `prj.conf` | priorities `0..15`, all preemptible, the kernel idle task at 16 |
| `CONFIG_FORCE_NO_ASSERT` | y | `prj.conf` | the `__ASSERT` statements in the measured operations expand to nothing |
| `CONFIG_DEBUG_OPTIMIZATIONS` | y | `prj.conf` | `-Og` for every test case |
| `CONFIG_POLL` | n | Kconfig default | `handle_poll_events()` in `kernel/queue.c` and `kernel/msg_q.c` returns `false`, the `#ifdef CONFIG_POLL` block of `z_impl_k_pipe_write()` is not compiled |
| `CONFIG_TRACING`, `CONFIG_OBJ_CORE` | n | Kconfig default | the `SYS_PORT_TRACING_*` macros in the measured operations expand to nothing |
| `CONFIG_ASSERT_ON_ERRORS`, `CONFIG_NO_RUNTIME_CHECKS` | not set | Kconfig default | `CHECKIF(expr)` from `zephyr/sys/check.h` stays `if (expr)` and runs wherever a measured operation holds it |
| `MEASUREMENT_COUNT` | 50000 | test source | transactions per series |
| `step` | `0..MESSAGE_SIZE_STEPS - 1`, `MESSAGE_SIZE_STEPS` = 9 | test source | one series per scenario and `step` |
| message size | `MESSAGE_SIZE_MIN << step`, `MESSAGE_SIZE_MIN` = 4 B | test source | domain `MESSAGE_SIZE_MIN..MESSAGE_SIZE_MAX`, `MESSAGE_SIZE_MAX` = `MESSAGE_SIZE_MIN << (MESSAGE_SIZE_STEPS - 1)` = 1024 B |

The `step` loop runs in `T_Self` and in the driver, so no value in any source selects a message
size. Nothing is subtracted from a recorded value.

1. `main()` fills the message buffers, turns the low power cache controller off
   (`SYSCON->LPCAC_CTRL`, so no instruction fetch is served from that cache inside a window),
   disables the SysTick (`BMTH_disable_sys_tick()`, so no tick interrupt falls into a window),
   and checks the cycle counter with `BMTH_check_read_validity()`.
2. `main()` initialises every series. A series whose two markers are taken in two tasks uses
   `BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS`, one whose markers are taken in
   one function uses `BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS`.
3. `I_Task` runs the scenarios in the order `self`, `up`, `down`. It aborts the tasks of one
   scenario before it creates the next, and resets both semaphores with `k_sem_reset()` before
   it creates the `down` tasks.
4. Per transaction, `BMTH_mwindow_open()` and `BMTH_mwindow_close()` bracket
   `BMTH_mseries_iterate()`, which records the value into the series. They run outside the
   measurement window, in the driver or in `T_Self`.
5. `I_Task` sums `values_outlier_count` over every series and reports through
   `BMTH_signalize_mseries_stop()`, which signals a failure if any series counted an outlier.

## Family members

| Directory | Test cases | Transfer object and what the send operation passes | Storage | Waiting receiver found by |
| --- | --- | --- | --- | --- |
| [`fifo`](fifo/README.md) | `fifo` (`CONFIG_BENCHMARK_TEST_ITC_FIFO`) | `k_fifo`: the address of the item, inserted at the tail of the list (`k_queue_append()`). No byte is copied. | `k_queue` list, linked through the first word of the item | `z_unpend_first_thread()` in `queue_insert()` |
| [`lifo`](lifo/README.md) | `lifo` (`CONFIG_BENCHMARK_TEST_ITC_LIFO`) | `k_lifo`: the address of the item, inserted at the head of the list (`k_queue_prepend()`). No byte is copied. | `k_queue` list, linked through the first word of the item | `z_unpend_first_thread()` in `queue_insert()` |
| [`stack`](stack/README.md) | `stack` (`CONFIG_BENCHMARK_TEST_ITC_STACK`) | `k_stack`: the address of the message as one `stack_data_t`. No byte is copied. | `stack_data_t` array given to `k_stack_init()` | `z_unpend_first_thread()` in `z_impl_k_stack_push()` |
| [`msgq`](msgq/README.md) | `msgq` (`CONFIG_BENCHMARK_TEST_ITC_MSGQ`) | `k_msgq`: a copy of `msg_size` bytes, `msg_size` fixed per object at `k_msgq_init()` | ring of `max_msgs` slots of `msg_size` bytes | `z_unpend_first_thread()` in `put_msg_in_queue()` |
| [`pipe`](pipe/README.md) | `pipe_async` (`CONFIG_BENCHMARK_TEST_ITC_PIPE_ASYNC`), `pipe_sync` (`CONFIG_BENCHMARK_TEST_ITC_PIPE_SYNC`) | `k_pipe`: a copy of `len` bytes, `len` an argument of each call | byte ring `pipe->buf`, its size an argument of `k_pipe_init()` | `pipe->waiting != 0` before `copy_to_pending_readers()` in `z_impl_k_pipe_write()` |
| [`mailbox`](mailbox/README.md) | `mailbox_async` (`CONFIG_BENCHMARK_TEST_ITC_MAILBOX_ASYNC`), `mailbox_sync` (`CONFIG_BENCHMARK_TEST_ITC_MAILBOX_SYNC`) | `k_mbox`: a `struct k_mbox_msg` pointing at the message. The receiver copies `size` bytes in `k_mbox_get()`. | `tx_msg_queue`, the wait queue of senders | `_WAIT_Q_FOR_EACH()` over `rx_msg_queue` with `mbox_message_match()` in `mbox_message_put()` |
