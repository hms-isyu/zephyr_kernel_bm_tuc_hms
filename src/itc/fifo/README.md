# fifo, one transaction through `k_fifo`

Set `CONFIG_BENCHMARK_TEST_ITC_FIFO=y` and rebuild.
Family and terms: [../README.md](../README.md).

`k_fifo` queues items by address and hands out the oldest one first. This test case times one
transaction through it.

## Participants

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_fifo_put()`, `k_fifo_get()` | send operation, receive operation | all |
| `T_Self` | sender and receiver | `self` |
| `T_SendUp`, `T_RecvUp` | sender, receiver; `receiver > sender` | `up` |
| `T_SendDown`, `T_RecvDown` | sender, receiver; `sender > receiver` | `down` |
| `T_ArmUp` | control task; `sender > control task`, `receiver > control task` | `up` |
| `T_ArmDown` | control task; `sender > control task`, `receiver > control task` | `down` |
| `test_fifo_self` | `struct k_fifo` | `self` |
| `test_fifo_high` | `struct k_fifo` | `up` |
| `test_fifo_low` | `struct k_fifo` | `down` |
| `test_item` | `test_item_t` | all |
| `rx_item` | `test_item_t *`, the return value of `k_fifo_get()` | all |

## Measurement

Triggers and marker placement are those of the family scenarios.

| Scenario | What the window holds |
| --- | --- |
| `self` | `k_fifo_put()` into storage, `k_fifo_get()` out of it |
| `up` | `k_fifo_put()` to the waiting receiver, one switch, `k_fifo_get()` |
| `down` | `k_fifo_put()` to the waiting receiver, the wait of `T_SendDown` for its next start up to its pend, one switch, `k_fifo_get()` |

`down - up` is the rest and return of `k_fifo_put()` plus the sender's wait for its next start up
to its pend, since both windows otherwise share the same `k_fifo_put()` up to readying the
receiver, one switch, and the same rest of `k_fifo_get()`, and holds no work of the storage.
`self - up` holds the storage path of both operations and no switch where `up` holds the waiting
receiver path and one switch, so `self - up` never isolates the storage.

| Test case | Load |
| --- | --- |
| `fifo` | message size |

`k_fifo_put()` and `k_fifo_get()` carry the address of `test_item` at every `step` and copy none of
its bytes.

Scenarios are driven in the order of the general readme.

## Compensation

None.

## Configuration

Shared configuration: [../README.md](../README.md).
