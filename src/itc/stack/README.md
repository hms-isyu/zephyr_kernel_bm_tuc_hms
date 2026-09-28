# stack, one transaction through `k_stack`

Set `CONFIG_BENCHMARK_TEST_ITC_STACK=y` and rebuild.
Family and terms: [../README.md](../README.md).

`k_stack` hands out the most recently pushed address first. This test case times one transaction
through it.

## Participants

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_stack_push()`, `k_stack_pop()` | send operation, receive operation | all |
| `T_Self` | sender and receiver | `self` |
| `T_SendUp`, `T_RecvUp` | sender, receiver; `receiver > sender` | `up` |
| `T_SendDown`, `T_RecvDown` | sender, receiver; `sender > receiver` | `down` |
| `T_ArmUp` | control task; `sender > control task`, `receiver > control task` | `up` |
| `T_ArmDown` | control task; `sender > control task`, `receiver > control task` | `down` |
| `test_stack_self` | `struct k_stack` | `self` |
| `test_stack_high` | `struct k_stack` | `up` |
| `test_stack_low` | `struct k_stack` | `down` |
| `stack_array_self`, `stack_array_high`, `stack_array_low` | `stack_data_t[STACK_MAX_ENTRIES]`, the storage of the object of the same suffix | `self`, `up`, `down` |
| `test_item` | `uint8_t[MESSAGE_SIZE_MAX]`, pushed as `(stack_data_t) test_item` | all |
| `rx_item` | the `stack_data_t` that `k_stack_pop()` writes | all |

## Measurement

Triggers and marker placement are those of the family scenarios.

| Scenario | What the window holds |
| --- | --- |
| `self` | `k_stack_push()` into storage, `k_stack_pop()` out of it |
| `up` | `k_stack_push()` to the waiting receiver, one switch, `k_stack_pop()` |
| `down` | `k_stack_push()` to the waiting receiver, the sender's wait for its next start up to its pend, one switch, `k_stack_pop()` |

`down - up` is the rest and return of `k_stack_push()` plus the sender's wait for its next start
up to its pend, since both windows otherwise share the same `k_stack_push()` up to readying the
receiver, one switch, and the same rest of `k_stack_pop()`, and holds no work of the storage.
`self - up` holds the storage path of both operations and no switch where `up` holds the waiting
receiver path and one switch, so `self - up` never isolates the storage.

| Test case | Load |
| --- | --- |
| `stack` | message size |

`k_stack_push()` and `k_stack_pop()` carry the address of `test_item` at every `step` and copy
none of its bytes.

Scenarios are driven in the order of the general readme.

## Compensation

None.

## Configuration

Shared configuration: [../README.md](../README.md#configuration).

| Setting | Value | Set in | Meaning |
| --- | --- | --- | --- |
| `STACK_MAX_ENTRIES` | 1 | test source | each object holds at most one address; `self` fills its storage and empties it again |
