# fifo, message hand-over via `k_fifo`

Set `CONFIG_BENCHMARK_TEST_IPC_FIFO=y` in `prj.conf` (Kconfig choice `BENCHMARK_TEST`, one
test case per build) and rebuild. Terminology: [../README.md](../README.md).

This test case times one message hand-over through `k_fifo`, which passes a reference: the
kernel takes the first word of the item for itself and hands the receiver the item's address.
Nothing of the message is copied. The storage is the `k_queue` list behind the object.

`k_fifo_put()` reads `list->tail` for the insert position and `k_lifo_put()` does not.
[../stack](../stack) keeps its word in an array instead of a list.

## Participants

Tasks and priorities: [../README.md](../README.md).

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_fifo_put()`, `k_fifo_get()` | the measured operations | all |
| `test_fifo_self`, `test_fifo_high`, `test_fifo_low` | `struct k_fifo`, one per scenario, each created before its scenario starts | `self`, `up`, `down` |
| `test_item` | `test_item_t`: the field `reserved`, which the kernel takes as the list link, then `MESSAGE_SIZE_MAX` payload bytes | all |
| `rx_item` | the pointer the get returns into | all |

The payload is filled once in `main()` and read by nothing afterwards. The item and the return
path are identical to [../lifo](../lifo) and [../stack](../stack), which is the condition for
the pairwise differences among the three.

## Measurement

| Scenario | Measurement window | What the window holds |
| --- | --- | --- |
| `self` | both markers in `T_Self` | the put and the get, through the list |
| `up` | start in `T_SendUp`, stop in `T_RecvUp` | the put, the switch into the receiver, the rest of the get |
| `down` | start in `T_SendDown`, stop in `T_RecvDown` | the put, the sender's block, the switch, the rest of the get |

`fifo - lifo` at any scenario is the `list->tail` read, which `queue_insert()` performs before
it looks for a waiting receiver and therefore on every path. `fifo - stack` at `self` is the
storage, where the list is entered; at `up` and `down` neither side reaches its storage and
that subtraction is the entry work of the two sends instead. `self - up` is not the storage on
its own, because `up` carries the switch as well.

No measured operation reads the size.

## Analysis

Expected of every scenario: the `MESSAGE_SIZE_STEPS` series of one scenario agree, and a
spread across them is not a cost of message size.

Expected of `fifo - lifo`: one `list->tail` read. The queue holds at most one item, so both
sides insert into an empty list and the insert position itself cannot separate them.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option (`prj.conf`) | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_POLL` | n | nothing is executed for it on any path here. Under `y`, `handle_poll_events()` at the end of `queue_insert()` would add a poll walk and a reschedule to the `self` path, which the cross-task paths return before reaching, so `self - up` would carry that difference as well. |
