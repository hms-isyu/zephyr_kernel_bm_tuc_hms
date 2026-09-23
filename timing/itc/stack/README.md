# stack, message hand-over via `k_stack`

Set `CONFIG_BENCHMARK_TEST_IPC_STACK=y` in `prj.conf` (Kconfig choice `BENCHMARK_TEST`, one
test case per build) and rebuild. Terminology: [../README.md](../README.md).

This test case times one message hand-over through `k_stack`, which carries a single word by
value. The test pushes the address of the message, so the message itself does not move. The
storage is an array, sized when the object is created.

It carries the same address as [../fifo](../fifo) and [../lifo](../lifo), whose item reserves
its first word for the `k_queue` list link (`test_item_t.reserved` in both). `k_stack` takes
nothing inside the message.

## Participants

Tasks and priorities: [../README.md](../README.md).

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_stack_push()`, `k_stack_pop()` | the measured operations | all |
| `test_stack_self`, `test_stack_high`, `test_stack_low` | `struct k_stack`, one per scenario, `STACK_MAX_ENTRIES` = 1, each created before its scenario starts | `self`, `up`, `down` |
| `stack_array_self`, `stack_array_high`, `stack_array_low` | one `stack_data_t` entry each, the storage of each object | one each |
| `test_item` | `uint8_t[MESSAGE_SIZE_MAX]`, its address is what is pushed | all |
| `rx_item` | the `stack_data_t` the pop writes into | all |

`test_item` is filled once in `main()` and read by nothing afterwards.

## Measurement

| Scenario | Measurement window | What the window holds |
| --- | --- | --- |
| `self` | both markers in `T_Self` | the push and the pop, through the array |
| `up` | start in `T_SendUp`, stop in `T_RecvUp` | the push, the switch into the receiver, the rest of the pop |
| `down` | start in `T_SendDown`, stop in `T_RecvDown` | the push, the sender's block, the switch, the rest of the pop |

`self - up` is not the array on its own, because `up` carries the switch as well.
`stack - fifo` and `stack - lifo` read the storage at `self`, where the array is entered; at
`up` and `down` neither side reaches its storage and the same subtraction is the entry work of
the two sends.

No measured operation reads the size. `STACK_MAX_ENTRIES` = 1 is the one message ever in flight.

## Analysis

Expected of every scenario: the `MESSAGE_SIZE_STEPS` series of one scenario agree, and a
spread across them is not a cost of message size.

Expected of `stack - fifo` and `stack - lifo` at `self`: the array against the list, at the
same address in flight and the same tasks on both sides.

The array is empty at every push, so `k_stack_push()` never takes its refusal path, the one
place in this test case where a send could fail. The compare that guards it is inside the
window and its return value is not read, which holds the number on one path at every step.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option (`prj.conf`) | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_ASSERT_ON_ERRORS`, `CONFIG_NO_RUNTIME_CHECKS` | not set | `CHECKIF()`, from `zephyr/sys/check.h`, keeps its `if (expr)` form, so the full check of `k_stack_push()` is a real compare inside the window. Under either symbol it compiles to `if (0)` and leaves the measurement. |
