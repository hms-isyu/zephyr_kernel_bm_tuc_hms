# msgq, message transfer via `k_msgq`

Set `CONFIG_BENCHMARK_TEST_IPC_MSGQ=y` in `prj.conf` (Kconfig choice `BENCHMARK_TEST`, one
test case per build) and rebuild. Terminology: [../README.md](../README.md).

This test case times one message transfer through `k_msgq`, which copies the bytes. The
storage is a ring of fixed size slots. With a receiver waiting the message is copied once,
into that receiver's buffer; with none it is copied into the ring and back out, twice.

The size is a property of the object, fixed at `k_msgq_init()`, where `k_pipe` takes it as an
argument of each call, so sweeping the size here means sweeping over objects.
`CONFIG_BENCHMARK_TEST_IPC_MAILBOX_ASYNC` runs the same scenarios over `k_mbox`, which copies
once in all of them, so that pair separates the copy from the rest.

## Participants

Tasks and priorities: [../README.md](../README.md).

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_msgq_put()`, `k_msgq_get()` | the measured operations | all |
| `test_msgq_self[step]`, `test_msgq_high[step]`, `test_msgq_low[step]` | `struct k_msgq`, one per scenario and step, `msg_size` = the size at that step, `MSGQ_MAX_MSGS` = 1, all created before the first transaction | `self`, `up`, `down` |
| `msgq_buffer_self`, `msgq_buffer_high`, `msgq_buffer_low` | the ring of each object, `MESSAGE_SIZE_MAX` bytes per step | one each |
| `active_msgq` | names the object of the current step to sender and receiver | `up`, `down` |
| `tx_buffer`, `rx_buffer` | `uint8_t[MESSAGE_SIZE_MAX]` each, source and destination of the copy | all |

Sender and receiver read `active_msgq` after their release, so the object of the step is
selected before the start marker and outside every window.

## Measurement

| Scenario | Measurement window | What the window holds | Copies |
| --- | --- | --- | --- |
| `self` | both markers in `T_Self` | the put and the get, through the ring | 2 |
| `up` | start in `T_SendUp`, stop in `T_RecvUp` | the put, the switch into the receiver, the rest of the get | 1 |
| `down` | start in `T_SendDown`, stop in `T_RecvDown` | the put, the sender's block, the switch, the rest of the get | 1 |

`self - up` carries the second copy and the switch at once, so it isolates neither. The copy is
read against the mailbox test case named above, at the same scenario.

The sweep needs one object per step, since `k_msgq_init()` fixes the size. `MSGQ_MAX_MSGS` = 1
is the one message ever in flight and is not swept.

## Analysis

Expected of every scenario: the value follows the message size, which is the length of
every copy. Expected of `self` against the two cross-task scenarios: the larger dependence on
the size, `self` moving the message twice where they move it once.

One message occupies the whole ring and `used_msgs` returns to 0 after every transaction, so
the write never wraps, no copy is ever split, and neither operation reaches its waiting path.
That holds the number on one path at every step.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option (`prj.conf`) | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_POLL` | n | the poll work sits on the branch that writes the ring, which is the `self` path. Under `y` the `self` number gains it and the two cross-task numbers do not, which is a dependency of any comparison between them. |
