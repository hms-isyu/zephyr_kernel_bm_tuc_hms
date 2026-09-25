# mailbox, one transaction through `k_mbox`

This directory holds two test cases. Set one of the symbols in `prj.conf` and rebuild:

* `CONFIG_BENCHMARK_TEST_ITC_MAILBOX_ASYNC=y` builds [mailbox_async.c](mailbox_async.c), test
  case `mailbox_async`, send operation `k_mbox_async_put()`.
* `CONFIG_BENCHMARK_TEST_ITC_MAILBOX_SYNC=y` builds [mailbox_sync.c](mailbox_sync.c), test case
  `mailbox_sync`, send operation `k_mbox_put()`.

Family, terms and shared configuration: [../README.md](../README.md).

These test cases time one transaction through `k_mbox`. Where [`../msgq`](../msgq)
(`CONFIG_BENCHMARK_TEST_ITC_MSGQ`) and `pipe_async` ([`../pipe`](../pipe),
`CONFIG_BENCHMARK_TEST_ITC_PIPE_ASYNC`) copy the message twice in `self`, `mailbox_async` copies
it once.

`k_mbox_async_put()` returns once the message is matched or queued. `k_mbox_put()` pends the
sender until the receiver has taken the message, so a task cannot receive its own message.

## Participants

| Name | What it is | Test case, scenario |
| --- | --- | --- |
| `k_mbox_async_put()`, `k_mbox_get()` | send operation, receive operation | `mailbox_async` |
| `k_mbox_put()`, `k_mbox_get()` | send operation, receive operation | `mailbox_sync` |
| `test_mbox_self` | `struct k_mbox` | `mailbox_async`, `self` |
| `test_mbox_high`, `test_mbox_low` | `struct k_mbox`: `test_mbox_high` in `up`, `test_mbox_low` in `down` | both, `up` and `down` |
| `tx_msg`, `rx_msg` | the `struct k_mbox_msg` of sender and receiver | both |
| `message_size` | the message size of the current `step`, written into `tx_msg.size` and `rx_msg.size` | both |
| `arm_descriptors()` | sets `tx_msg.tx_target_thread` and `rx_msg.rx_source_thread` to `K_ANY` and both `size` fields to `message_size`, before every transaction and outside every window | both |
| `tx_buffer`, `rx_buffer` | `uint8_t[MESSAGE_SIZE_MAX]` each, source and destination of the copy | both |
| `async_msg_free` | kernel `k_stack` of `CONFIG_NUM_MBOX_ASYNC_MSGS` `struct k_mbox_async` elements. `mbox_async_alloc()` takes one with `k_stack_pop()` in `k_mbox_async_put()`, `mbox_async_free()` gives it back with `k_stack_push()` in `mbox_message_dispose()`. | `mailbox_async` |
| `mbox_message_dispose()` | runs in the receiver after the copy. In `mailbox_async` it calls `mbox_async_free()`, in `mailbox_sync` it readies the sender and calls `z_reschedule_unlocked()`. | both |

`mbox_message_match()` overwrites `tx_target_thread` and `rx_source_thread` with the matched
task. `arm_descriptors()` sets both back, so every match passes on its `K_ANY` compare.

## Measurement

Triggers and marker placement are those of the family scenarios, the `rendezvous_*` series
included.

| Test case | Series | What the window holds |
| --- | --- | --- |
| `mailbox_async` | `self_send` | `k_mbox_async_put()` onto `tx_msg_queue`, `k_mbox_get()` from it |
| `mailbox_async` | `send_to_higher_prio` | `k_mbox_async_put()` to the waiting receiver, one switch, `k_mbox_get()` |
| `mailbox_async` | `send_to_lower_prio` | `k_mbox_async_put()` to the waiting receiver, `k_sem_take()` of `T_SendDown` up to its pend, one switch, `k_mbox_get()` |
| `mailbox_sync` | `send_to_higher_prio` | `k_mbox_put()` to the waiting receiver up to the pend of `T_SendUp`, one switch, `k_mbox_get()` |
| `mailbox_sync` | `rendezvous_to_higher_prio` | `k_mbox_put()` to the waiting receiver up to the pend of `T_SendUp`, one switch, `k_mbox_get()`, `k_sem_take()` of `T_RecvUp` up to its pend, one switch, the return of `k_mbox_put()` |
| `mailbox_sync` | `rendezvous_to_lower_prio` | `k_mbox_put()` to the waiting receiver up to the pend of `T_SendDown`, one switch, `k_mbox_get()` up to its `mbox_message_dispose()`, one switch, the return of `k_mbox_put()` |
| `mailbox_sync` | `send_to_lower_prio` | `k_mbox_put()` to the waiting receiver up to the pend of `T_SendDown`, one switch, `k_mbox_get()` up to its `mbox_message_dispose()`, one switch, the return of `k_mbox_put()`, `k_sem_take()` of `T_SendDown` up to its pend, one switch, the rest of `k_mbox_get()` |

`mailbox_async - mailbox_sync` in `send_to_higher_prio`: both windows hold the match, one switch
and the one copy. On the `mailbox_async` side they hold `mbox_async_alloc()`, the copy of
`tx_msg` into the element, the switch out of `z_reschedule()`, and `mbox_async_free()`. On the
`mailbox_sync` side they hold the switch out of `z_pend_curr()` in `mbox_message_put()`, and the
ready of the sender and `z_reschedule_unlocked()` in `mbox_message_dispose()`. In
`send_to_lower_prio` the two do not pair the same way: `mailbox_async` holds one switch,
`mailbox_sync` three.

`rendezvous_to_higher_prio - send_to_higher_prio` = `k_sem_take()` of `T_RecvUp` up to its
pend, one switch to `T_SendUp`, and the return of `k_mbox_put()`.
`send_to_lower_prio - rendezvous_to_lower_prio` = `k_sem_take()` of `T_SendDown` up to its pend,
one switch to `T_RecvDown`, and the return of `mbox_message_dispose()` and `k_mbox_get()`.

In `up` and `down` the receiver is the first entry of `rx_msg_queue`, so `mbox_message_put()`
matches on the first entry it walks, and no message is queued on `tx_msg_queue`; only
`self_send` queues one. `mbox_async_alloc()` and `mbox_async_free()` run once per transaction
of `mailbox_async`: `k_stack_pop()` on a non-empty array and `k_stack_push()` with no waiting
receiver, its `CHECKIF()` compare included, the two paths that `self` of [`../stack`](../stack)
(`CONFIG_BENCHMARK_TEST_ITC_STACK`) times.

One object per scenario serves every `step`, the message size travelling in `tx_msg.size` and
`rx_msg.size`.

## Compensation

No series of these test cases times the same path without the transaction.

## Analysis

Expected: every series grows with the message size, the length of the one copy. Expected:
`self_send` of `mailbox_async` grows less with the message size than `self_send` of `msgq` and
`pipe_async`, and its cross-task series grow with the message size as theirs do.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_NUM_MBOX_ASYNC_MSGS` | 10, the Kconfig default | `k_mbox_async_put()`, `mbox_async_alloc()` and `mbox_async_free()` are compiled only for a value > 0, so `mailbox_async` builds only then. One element is taken at a time. |
