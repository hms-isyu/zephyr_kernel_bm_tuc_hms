# lifo, one transaction through `k_lifo`

Set `CONFIG_BENCHMARK_TEST_ITC_LIFO=y` and rebuild.
Family and terms: [../README.md](../README.md).

`k_lifo` hands out the most recently inserted item first. This test case times one transaction
through it.

## Participants

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_lifo_put()`, `k_lifo_get()` | send operation, receive operation | all |
| `T_Self` | sender and receiver | `self` |
| `T_SendUp`, `T_RecvUp` | sender, receiver; `receiver > sender` | `up` |
| `T_SendDown`, `T_RecvDown` | sender, receiver; `sender > receiver` | `down` |
| `T_ArmUp` | control task; `sender > control task`, `receiver > control task` | `up` |
| `T_ArmDown` | control task; `sender > control task`, `receiver > control task` | `down` |
| `test_lifo_self` | `struct k_lifo` | `self` |
| `test_lifo_high` | `struct k_lifo` | `up` |
| `test_lifo_low` | `struct k_lifo` | `down` |
| `test_item` | `test_item_t` | all |
| `rx_item` | `test_item_t *`, the return value of `k_lifo_get()` | all |

## Measurement

Triggers and marker placement are those of the family scenarios.

| Scenario | What the window holds |
| --- | --- |
| `self` | `k_lifo_put()` into storage, `k_lifo_get()` out of it |
| `up` | `k_lifo_put()` to the waiting receiver, one switch, `k_lifo_get()` |
| `down` | `k_lifo_put()` to the waiting receiver, the sender's wait for its next start up to its pend, one switch, `k_lifo_get()` |

`down - up` is the rest and return of `k_lifo_put()` plus the sender's wait for its next start up
to its pend, since both windows otherwise share the same `k_lifo_put()` up to readying the receiver, one
switch, and the same rest of `k_lifo_get()`, and holds no work of the storage. `self - up` holds
the storage path of both operations and no switch where `up` holds the waiting receiver path and
one switch, so `self - up` never isolates the storage.

| Test case | Load |
| --- | --- |
| `lifo` | message size |

`k_lifo_put()` and `k_lifo_get()` carry the address of `test_item` at every `step` and copy none of
its bytes.

Scenarios are driven in the order of the general readme.

## Compensation

None.

## Configuration

Shared configuration: [../README.md](../README.md).
