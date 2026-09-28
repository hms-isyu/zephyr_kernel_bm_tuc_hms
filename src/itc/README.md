# Inter-task communication

The test cases under `src/itc` time one message passed from one task to another through a
Zephyr kernel object.

## Terminology

| Term | Meaning |
| --- | --- |
| sender | The task that calls the send operation and takes the start marker. |
| receiver | The task that calls the receive operation and takes the stop marker once it returns. |
| control task | The task that starts the receiver and the sender for one transaction and advances the message size. |
| transfer object | The kernel object the message passes through, one kind per directory. Its send operation takes the message in, its receive operation hands it out. |
| storage | Where the transfer object keeps a message while no receiver waits for it. |
| waiting receiver | A receiver already waiting inside the receive operation when the send operation runs. |
| transaction | One send operation and the receive operation that takes its message. |
| message size, step | The bytes one message holds, and the index of one message size. The message size is the load. A transfer object that passes the bytes copies message size bytes per copy. A transfer object that passes an address copies nothing, whatever the message size. |

## Scenarios

| Scenario | Trigger | Measured path |
| --- | --- | --- |
| `self` | One task, sender and receiver at once, reaches its start marker. | The send operation into the storage and the receive operation out of it. No switch. The only scenario that uses the storage. |
| `up` | The control task starts the sender, `receiver > sender`. | The send operation up to the point where it readies the waiting receiver, the switch into the receiver out of the send operation, the rest of the receive operation. |
| `down` | The control task starts the sender, `sender > receiver`. | The send operation up to the point where it readies the waiting receiver, the rest of the send operation and its return, the sender's wait for its next start up to the point where it pends, the switch into the receiver, the rest of the receive operation. A send operation that pends until the receiver has taken the message holds the switch in place of its return and that wait. |

## Measurement principle

1. The output value is the cycle count from the start marker to the stop marker.
2. A measurement window holds one transaction and, where the scenario needs it, the wait in
   which the sender or the receiver gives up the CPU, and nothing else.
3. The control task has a lower priority than the sender and the receiver, so it runs only once both
   are blocked and both markers are taken, and none of its statements falls inside a window.
4. The receiver is a waiting receiver before the control task starts the sender, so every send
   operation of `up` and `down` finds it.
5. The control task starts one transaction at a time; starting the sender hands over the CPU.
6. One build runs every message size, `step` ascending. Every transaction of a scenario at one
   `step` runs before the next `step`, and its output values are recorded apart from every other
   scenario and `step`.
7. Scenarios run one after the other, in the order `self`, `up`, `down`, and no task of one
   scenario runs during another. A test case with no `self` scenario runs `up`, `down`.

## Configuration

A test case is built by setting its `CONFIG_BENCHMARK_TEST_ITC_*` symbol to y in `prj.conf`.
The symbols belong to the Kconfig choice `BENCHMARK_TEST`, so one build holds one test case.
Shared by every test case:

| Setting | Value | Set in | Meaning |
| --- | --- | --- | --- |
| `MEASUREMENT_COUNT` | 50000 | test source | transactions per scenario and `step` |
| `MESSAGE_SIZE_STEPS` | 9 | test source | `step` = `0..MESSAGE_SIZE_STEPS - 1` |
| `MESSAGE_SIZE_MIN` | 4 B | test source | message size = `MESSAGE_SIZE_MIN << step`, `MESSAGE_SIZE_MIN..MESSAGE_SIZE_MAX`, `MESSAGE_SIZE_MAX` = `MESSAGE_SIZE_MIN << (MESSAGE_SIZE_STEPS - 1)` = 1024 B |

## Family members

| Directory | Source | Description |
| --- | --- | --- |
| [`fifo`](fifo/README.md) | [`fifo.c`](fifo/fifo.c) | Passes an address; `self` appends the message at the tail of the storage. |
| [`lifo`](lifo/README.md) | [`lifo.c`](lifo/lifo.c) | Passes an address; `self` prepends the message at the head of the storage. |
| [`stack`](stack/README.md) | [`stack.c`](stack/stack.c) | Passes an address into a storage of a capacity fixed per object. |
| [`msgq`](msgq/README.md) | [`msgq.c`](msgq/msgq.c) | Passes the bytes, the message size fixed per object. |
| [`pipe`](pipe/README.md) | [`pipe_async.c`](pipe/pipe_async.c), [`pipe_sync.c`](pipe/pipe_sync.c) | Passes the bytes, the message size given per call. |
| [`mailbox`](mailbox/README.md) | [`mailbox_async.c`](mailbox/mailbox_async.c), [`mailbox_sync.c`](mailbox/mailbox_sync.c) | Passes the bytes; the storage holds the sender's message descriptor, and the receive operation copies the bytes. |
