# stack, one transaction through `k_stack`

Set `CONFIG_BENCHMARK_TEST_ITC_STACK=y` in `prj.conf` and rebuild. The symbol builds
[stack.c](stack.c). Family, terms and shared configuration: [../README.md](../README.md).

This test case times one transaction through `k_stack`. [`../fifo`](../fifo)
(`CONFIG_BENCHMARK_TEST_ITC_FIFO`) and [`../lifo`](../lifo) (`CONFIG_BENCHMARK_TEST_ITC_LIFO`)
pass an address of the same kind and link the item through its first word. This test case writes
no word inside the message, and in `self` it sets an array store and load against their list
insert and removal.

## Participants

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_stack_push()`, `k_stack_pop()` | send operation, receive operation | all |
| `test_stack_self`, `test_stack_high`, `test_stack_low` | `struct k_stack`: `test_stack_self` in `self`, `test_stack_high` in `up`, `test_stack_low` in `down`. Capacity `STACK_MAX_ENTRIES` = 1, all three initialised by `I_Task` before the first scenario. | `self`, `up`, `down` |
| `stack_array_self`, `stack_array_high`, `stack_array_low` | `stack_data_t[STACK_MAX_ENTRIES]`, the storage of the object of the same suffix: `stack_array_self` in `self`, `stack_array_high` in `up`, `stack_array_low` in `down` | `self`, `up`, `down` |
| `test_item` | `uint8_t[MESSAGE_SIZE_MAX]`, pushed as `(stack_data_t) test_item` | all |
| `rx_item` | the `stack_data_t` that `k_stack_pop()` writes | all |

## Measurement

Triggers and marker placement are those of the family scenarios.

| Scenario | What the window holds |
| --- | --- |
| `self` | `k_stack_push()` into the array, `k_stack_pop()` out of it |
| `up` | `k_stack_push()` to the waiting receiver, one switch, `k_stack_pop()` |
| `down` | `k_stack_push()` to the waiting receiver, `k_sem_take()` of `T_SendDown` up to its pend, one switch, `k_stack_pop()` |

The array is empty at every `k_stack_push()`: in `self` the preceding `k_stack_pop()` emptied
it, in `up` and `down` nothing is stored. `CHECKIF(stack->next == stack->top)` in
`z_impl_k_stack_push()` runs and is false at every push, and `k_stack_push()` never returns
`-ENOMEM`.

`stack - fifo` at every scenario holds, ahead of the check for a waiting receiver, the `CHECKIF()`
compare against the `list->tail` read of `k_fifo_put()`. At `self` it also holds the array store
and load against the list insert and removal. `stack - lifo` is the same without the `list->tail`
read.

## Compensation

No series of this test case times the same path without the transaction.

## Analysis

Expected: the `MESSAGE_SIZE_STEPS` series of one scenario agree, since no measured operation
reads the message size.

## Configuration

Shared configuration: [../README.md](../README.md).
