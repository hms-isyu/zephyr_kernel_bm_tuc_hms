# lifo, one transaction through `k_lifo`

Set `CONFIG_BENCHMARK_TEST_ITC_LIFO=y` in `prj.conf` and rebuild. The symbol builds
[lifo.c](lifo.c). Family, terms and shared configuration: [../README.md](../README.md).

This test case times one transaction through `k_lifo`. Its source is that of
[`../fifo`](../fifo) (`CONFIG_BENCHMARK_TEST_ITC_FIFO`) with `k_lifo` in place of `k_fifo`. The
two differ in the insert position.

## Participants

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_lifo_put()`, `k_lifo_get()` | send operation, receive operation | all |
| `test_lifo_self`, `test_lifo_high`, `test_lifo_low` | `struct k_lifo`: `test_lifo_self` in `self`, `test_lifo_high` in `up`, `test_lifo_low` in `down`. All three initialised by `I_Task` before the first scenario. | `self`, `up`, `down` |
| `test_item` | `test_item_t`: the pointer `reserved`, then `payload[MESSAGE_SIZE_MAX]` | all |
| `rx_item` | `test_item_t *`, the return value of `k_lifo_get()` | all |

No measured operation reads `payload`. `queue_insert()` writes `reserved` as the list node
(`sys_sfnode_init()`) only on its storage path, which only `self` takes.

## Measurement

Triggers and marker placement are those of the family scenarios.

| Scenario | What the window holds |
| --- | --- |
| `self` | `k_lifo_put()` into the list, `k_lifo_get()` out of it |
| `up` | `k_lifo_put()` to the waiting receiver, one switch, `k_lifo_get()` |
| `down` | `k_lifo_put()` to the waiting receiver, `k_sem_take()` of `T_SendDown` up to its pend, one switch, `k_lifo_get()` |

`lifo - fifo` at every scenario is minus the `list->tail` read (`sys_sflist_peek_tail()`) that
`queue_insert()` makes under `is_append`, before its check for a waiting receiver. The insert
position does not enter it: in `up` and `down` nothing is inserted, and in `self` the list is
empty at every put, so no scenario inserts into a non-empty list.

## Compensation

No series of this test case times the same path without the transaction.

## Analysis

Expected: the `MESSAGE_SIZE_STEPS` series of one scenario agree, since no measured operation
reads the message size. Expected: `lifo - fifo` < 0 and of the same size at `self`, `up` and
`down`, since the `list->tail` read sits before the check for a waiting receiver.

## Configuration

Shared configuration: [../README.md](../README.md).
