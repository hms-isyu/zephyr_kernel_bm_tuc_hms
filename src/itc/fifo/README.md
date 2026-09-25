# fifo, one transaction through `k_fifo`

Set `CONFIG_BENCHMARK_TEST_ITC_FIFO=y` in `prj.conf` and rebuild. The symbol builds
[fifo.c](fifo.c). Family, terms and shared configuration: [../README.md](../README.md).

This test case times one transaction through `k_fifo`. Its source is that of
[`../lifo`](../lifo) (`CONFIG_BENCHMARK_TEST_ITC_LIFO`) with `k_fifo` in place of `k_lifo`.

## Participants

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_fifo_put()`, `k_fifo_get()` | send operation, receive operation | all |
| `test_fifo_self`, `test_fifo_high`, `test_fifo_low` | `struct k_fifo`: `test_fifo_self` in `self`, `test_fifo_high` in `up`, `test_fifo_low` in `down`. All three initialised by `I_Task` before the first scenario. | `self`, `up`, `down` |
| `test_item` | `test_item_t`: the pointer `reserved`, then `payload[MESSAGE_SIZE_MAX]` | all |
| `rx_item` | `test_item_t *`, the return value of `k_fifo_get()` | all |

No measured operation reads `payload`. `queue_insert()` writes `reserved` as the list node
(`sys_sfnode_init()`) only on its storage path, which only `self` takes.

## Measurement

Triggers and marker placement are those of the family scenarios.

| Scenario | What the window holds |
| --- | --- |
| `self` | `k_fifo_put()` into the list, `k_fifo_get()` out of it |
| `up` | `k_fifo_put()` to the waiting receiver, one switch, `k_fifo_get()` |
| `down` | `k_fifo_put()` to the waiting receiver, `k_sem_take()` of `T_SendDown` up to its pend, one switch, `k_fifo_get()` |

`fifo - lifo` at every scenario: `queue_insert()` reads `list->tail` (`sys_sflist_peek_tail()`)
under `is_append`, before its check for a waiting receiver, and `k_lifo_put()` does not. In
`self` the list is empty at every `k_fifo_put()`, so no scenario inserts at the tail of a
non-empty list, and `fifo - lifo` is the `list->tail` read and not a difference of insert
position.

`fifo - stack` ([`../stack`](../stack), `CONFIG_BENCHMARK_TEST_ITC_STACK`) ahead of the check for
a waiting receiver: the send operation of `stack` runs the `CHECKIF()` compare that `k_fifo_put()`
does not, `k_fifo_put()` reads `list->tail`.

## Compensation

No series of this test case times the same path without the transaction.

## Analysis

Expected: the `MESSAGE_SIZE_STEPS` series of one scenario agree, since no measured operation
reads the message size. Expected: `fifo - lifo` of the same size at `self`, `up` and `down`,
since the `list->tail` read sits before the check for a waiting receiver.

## Configuration

Shared configuration: [../README.md](../README.md).
