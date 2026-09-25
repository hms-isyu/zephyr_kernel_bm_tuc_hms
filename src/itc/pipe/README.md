# pipe, one transaction through `k_pipe`

This directory holds two test cases. Set one of the symbols in `prj.conf` and rebuild:

* `CONFIG_BENCHMARK_TEST_ITC_PIPE_ASYNC=y` builds [pipe_async.c](pipe_async.c), test case
  `pipe_async`.
* `CONFIG_BENCHMARK_TEST_ITC_PIPE_SYNC=y` builds [pipe_sync.c](pipe_sync.c), test case
  `pipe_sync`.

Family, terms and shared configuration: [../README.md](../README.md).

These test cases time one transaction through `k_pipe`. The pipe hands the message straight to
a waiting receiver, or, with none, buffers it in its ring, out of which the receive operation
copies it. One object per scenario serves every `step`, where [`../msgq`](../msgq)
(`CONFIG_BENCHMARK_TEST_ITC_MSGQ`) needs one per scenario and `step`.

`pipe_async` and `pipe_sync` differ in the ring size given to `k_pipe_init()`. With a ring of
size 0, a `k_pipe_write()` without a waiting
receiver takes no byte and pends until a receiver arrives.

## Participants

| Name | What it is | Test case, scenario |
| --- | --- | --- |
| `k_pipe_write()`, `k_pipe_read()` | send operation, receive operation | both |
| `test_pipe_self`, `test_pipe_high`, `test_pipe_low` | `struct k_pipe`: `test_pipe_self` in `self`, `test_pipe_high` in `up`, `test_pipe_low` in `down`. Each with its ring `pipe_buffer_self`, `pipe_buffer_high`, `pipe_buffer_low` of `PIPE_BUFFER_SIZE` = `MESSAGE_SIZE_MAX` bytes. | `pipe_async` |
| `test_pipe` | `struct k_pipe`, `k_pipe_init(&test_pipe, NULL, 0U)` before `up` and again before `down` | `pipe_sync` |
| `active_message_size` | the message size of the current `step`, set by the driver before the first transaction of the step | both, `up` and `down` |
| `tx_buffer`, `rx_buffer` | `uint8_t[MESSAGE_SIZE_MAX]` each, source and destination of the copy | both |

The receiver loads `active_message_size` before it pends in `k_pipe_read()`, which keeps it as
`len` in the `pipe_buf_spec` on the receiver's stack. The sender loads it after its start
marker, one load inside the window. `T_Self` holds the message size in a local variable set
before the transaction loop.

## Measurement

Triggers and marker placement are those of the family scenarios, the `rendezvous_*` series
included.

| Test case | Symbol | Ring size | Scenarios |
| --- | --- | --- | --- |
| `pipe_async` | `CONFIG_BENCHMARK_TEST_ITC_PIPE_ASYNC` | `PIPE_BUFFER_SIZE` | `self`, `up`, `down` |
| `pipe_sync` | `CONFIG_BENCHMARK_TEST_ITC_PIPE_SYNC` | 0 | `up`, `down` |

| Test case | Series | What the window holds | Copies |
| --- | --- | --- | --- |
| `pipe_async` | `self_send` | `k_pipe_write()` into the ring, `k_pipe_read()` out of it | 2 |
| both | `send_to_higher_prio` | `k_pipe_write()` to the waiting receiver, one switch, `k_pipe_read()` | 1 |
| both | `send_to_lower_prio` | `k_pipe_write()` to the waiting receiver, `k_sem_take()` of `T_SendDown` up to its pend, one switch, `k_pipe_read()` | 1 |
| `pipe_sync` | `rendezvous_to_higher_prio` | `k_pipe_write()` to the waiting receiver, one switch, `k_pipe_read()`, `k_sem_take()` of `T_RecvUp` up to its pend, one switch, the return of `k_pipe_write()` | 1 |
| `pipe_sync` | `rendezvous_to_lower_prio` | `k_pipe_write()` to the waiting receiver and its return. No switch. | 1 |

`rendezvous_to_higher_prio - send_to_higher_prio` = `k_sem_take()` of `T_RecvUp` up to its
pend, one switch to `T_SendUp`, and the return of `k_pipe_write()`.
`send_to_lower_prio - rendezvous_to_lower_prio` = `k_sem_take()` of `T_SendDown` up to its pend,
one switch to `T_RecvDown`, and the rest of `k_pipe_read()`.

`pipe_async - pipe_sync` in `send_to_higher_prio` and in `send_to_lower_prio`: both send
operations hand all `len` bytes to the waiting receiver, and neither enters its ring. After the
receiver is readied, `k_pipe_read()` tests `pipe_full()` again: true for the ring of size 0 of
`pipe_sync`, which then calls `z_sched_wake_all(&pipe->space, 0, NULL)` on an empty wait queue,
false for the empty ring of `pipe_async`. `pipe_async - pipe_sync` is minus that call and holds
no work of the storage.

`k_pipe_write()` never reaches `wait_for(&pipe->space, ...)`: in `self` the ring has room for
the message, in `up` and `down` the waiting receiver takes all `len` bytes.

In `self`, `test_pipe_self` is not reset between steps. A step starts at the ring position
`(Σ_{k < step} MEASUREMENT_COUNT · (MESSAGE_SIZE_MIN << k)) mod PIPE_BUFFER_SIZE`. At `step`
0..4 that position is a multiple of the message size and no message crosses the end of the
ring. At `step` 5..8 one message in `PIPE_BUFFER_SIZE / (MESSAGE_SIZE_MIN << step)`, at `step` 8
every message, crosses the end of the ring and is copied in two pieces into it and in two pieces
out of it.

At `step` = `MESSAGE_SIZE_STEPS - 1` the message size equals `PIPE_BUFFER_SIZE`, so
`k_pipe_read()` in `self` starts on a full ring: `pipe_full()` is true and it calls
`z_sched_wake_all(&pipe->space, 0, NULL)`, which it skips at every lower `step`.

## Compensation

No series of these test cases times the same path without the transaction.

## Analysis

Expected: every series grows with the message size, the length of every copy. Expected:
`self_send` grows more with the message size than the cross-task series, with two copies
against one. Expected: `self_send` at `step` 5..8 carries the two-piece copies of the messages
that cross the end of the ring, and at `step` 8 the `z_sched_wake_all()` call as well, so a
comparison across `step` in `self_send` holds these differences.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_KERNEL_COHERENCE` | not set | `z_impl_k_pipe_write()` takes the `copy_to_pending_readers()` branch on an empty pipe. Set, that branch calls `z_sched_wake_all(&pipe->data, 0, NULL)` instead and the bytes go through `ring_buf_put()`: `pipe_async` would copy twice in `up` and `down`, and `pipe_sync`, with a ring of size 0, would have no branch that moves the bytes to the receiver. |
