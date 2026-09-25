# msgq, one transaction through `k_msgq`

Set `CONFIG_BENCHMARK_TEST_ITC_MSGQ=y` in `prj.conf` and rebuild. The symbol builds
[msgq.c](msgq.c). Family, terms and shared configuration: [../README.md](../README.md).

This test case times one transaction through `k_msgq`. Against `mailbox_async`
([`../mailbox`](../mailbox), `CONFIG_BENCHMARK_TEST_ITC_MAILBOX_ASYNC`) it copies the message
once in `up` and `down`, as `mailbox_async` does, and twice in `self`, where `mailbox_async`
copies once.

## Participants

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_msgq_put()`, `k_msgq_get()` | send operation, receive operation | all |
| `test_msgq_self[step]`, `test_msgq_high[step]`, `test_msgq_low[step]` | `struct k_msgq`, one per scenario and `step`: `test_msgq_self` in `self`, `test_msgq_high` in `up`, `test_msgq_low` in `down`. `msg_size` = the message size at `step`, `max_msgs` = `MSGQ_MAX_MSGS` = 1, all initialised by `I_Task` before the first scenario. | `self`, `up`, `down` |
| `msgq_buffer_self[step]`, `msgq_buffer_high[step]`, `msgq_buffer_low[step]` | `MESSAGE_SIZE_MAX * MSGQ_MAX_MSGS` bytes per object of the same suffix. `k_msgq_init()` sets `buffer_end` = `buffer_start + msg_size * MSGQ_MAX_MSGS`, so the ring is the first `msg_size` bytes. | `self`, `up`, `down` |
| `active_msgq` | the object of the current `step`, set by the driver before the first transaction of the step | `up`, `down` |
| `tx_buffer`, `rx_buffer` | `uint8_t[MESSAGE_SIZE_MAX]` each, source and destination of the copy | all |

The receiver loads `active_msgq` before it pends in `k_msgq_get()`. The sender loads it after
its start marker, one load inside the window of `up` and `down`. `T_Self` holds the object of
the step in a local variable set before the transaction loop.

## Measurement

Triggers and marker placement are those of the family scenarios.

| Scenario | What the window holds | Copies |
| --- | --- | --- |
| `self` | `k_msgq_put()` into the ring, `k_msgq_get()` out of it | 2 |
| `up` | `k_msgq_put()` to the waiting receiver, one switch, `k_msgq_get()` | 1 |
| `down` | `k_msgq_put()` to the waiting receiver, `k_sem_take()` of `T_SendDown` up to its pend, one switch, `k_msgq_get()` | 1 |

Every copy is one `memcpy()` of `msg_size` bytes, in `put_msg_in_queue()` and in
`z_impl_k_msgq_get()`; a message is never split.

In `self`, `used_msgs` is 0 at every `k_msgq_put()` and 1 at every `k_msgq_get()`. The ring holds
one slot, so `write_ptr` and `read_ptr` reach `buffer_end` and are set back to `buffer_start`
at every transaction, at every `step`. In `up` and `down`, `used_msgs` stays 0, so
`k_msgq_put()` never takes its `z_pend_curr()` branch for a full queue.

`msgq - mailbox_async` at the same scenario and `step` holds the same number of copies of the
same length in `up` and `down`, one more copy of `msg_size` bytes on the `msgq` side in `self`,
and every other difference of the two transfer objects.

## Compensation

No series of this test case times the same path without the transaction.

## Analysis

Expected: every scenario grows with the message size, the length of every copy. Expected:
`self` grows more with the message size than `up` and `down`, with two copies against one.

## Configuration

Shared configuration: [../README.md](../README.md).
