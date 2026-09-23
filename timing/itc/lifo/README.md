# lifo, message hand-over via `k_lifo`

Set `CONFIG_BENCHMARK_TEST_IPC_LIFO=y` in `prj.conf` (Kconfig choice `BENCHMARK_TEST`, one
test case per build) and rebuild. Terminology: [../README.md](../README.md).

This test case times one message hand-over through `k_lifo`, which passes a reference over the
same `k_queue` layer and the same item as `k_fifo`: the kernel takes the first word of the item
for itself and hands the receiver the item's address. Nothing of the message is copied. The
storage is the `k_queue` list behind the object.

`k_lifo_put()` passes NULL as the insert position, `k_fifo_put()` passes what it read from
`list->tail`. Everything else in the two test cases is the same source over the same item at
the same priorities.

## Participants

Tasks and priorities: [../README.md](../README.md).

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_lifo_put()`, `k_lifo_get()` | the measured operations | all |
| `test_lifo_self`, `test_lifo_high`, `test_lifo_low` | `struct k_lifo`, one per scenario, each created before its scenario starts | `self`, `up`, `down` |
| `test_item` | `test_item_t`: the field `reserved`, which the kernel takes as the list link, then `MESSAGE_SIZE_MAX` payload bytes | all |
| `rx_item` | the pointer the get returns into | all |

The payload is filled once in `main()` and read by nothing afterwards. `test_item` is byte for
byte the item of [../fifo](../fifo), which is the condition for subtracting the two.

## Measurement

| Scenario | Measurement window | What the window holds |
| --- | --- | --- |
| `self` | both markers in `T_Self` | the put and the get, through the list |
| `up` | start in `T_SendUp`, stop in `T_RecvUp` | the put, the switch into the receiver, the rest of the get |
| `down` | start in `T_SendDown`, stop in `T_RecvDown` | the put, the sender's block, the switch, the rest of the get |

`self - up` is not the list on its own, because `up` carries the switch as well. The insert
position is read as `lifo - fifo`, at any of the three scenarios, since the `list->tail` read
happens before the waiting receiver is looked for and therefore on every path.

No measured operation reads the size.

## Analysis

Expected of every scenario: the `MESSAGE_SIZE_STEPS` series of one scenario agree, and a
spread across them is not a cost of message size.

Expected of `lifo - fifo`: one `list->tail` read, and nothing else. The queue holds at most one
item, so both sides insert into an empty list and reach the same insert position from different
arguments. Separating head from tail would need a list of two items, which no scenario here
produces, so this pair does not price the insertion end.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option (`prj.conf`) | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_POLL` | n | nothing is executed for it on any path here. Under `y`, `handle_poll_events()` at the end of `queue_insert()` would add a poll walk and a reschedule to the `self` path, which the cross-task paths return before reaching. The `k_fifo` build has to carry the same value, or `lifo - fifo` carries that difference too. |
