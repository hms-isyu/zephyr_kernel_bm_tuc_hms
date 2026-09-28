# mailbox, one transaction through `k_mbox`

This directory holds two test cases. Set one of the symbols and rebuild:

* `CONFIG_BENCHMARK_TEST_ITC_MAILBOX_ASYNC=y` selects test case `mailbox_async`.
* `CONFIG_BENCHMARK_TEST_ITC_MAILBOX_SYNC=y` selects test case `mailbox_sync`.

Family and terms: [../README.md](../README.md).

`k_mbox` matches a sender's message against a waiting receiver, or queues it if none waits; the
receiver copies it out. `k_mbox_async_put()` returns once the message is matched or queued,
`k_mbox_put()` once the receiver has taken it. These test cases time one transaction through it.

## Participants

| Name | What it is | Test case, scenario |
| --- | --- | --- |
| `k_mbox_async_put()`, `k_mbox_get()` | send operation, receive operation | `mailbox_async` |
| `k_mbox_put()`, `k_mbox_get()` | send operation, receive operation | `mailbox_sync` |
| `T_Self` | sender and receiver | `mailbox_async`, `self` |
| `T_SendUp`, `T_RecvUp` | sender, receiver; `receiver > sender` | both, `up` |
| `T_SendDown`, `T_RecvDown` | sender, receiver; `sender > receiver` | both, `down` |
| `T_ArmUp` | control task; `sender > control task`, `receiver > control task` | both, `up` |
| `T_ArmDown` | control task; `sender > control task`, `receiver > control task` | both, `down` |
| `test_mbox_self` | `struct k_mbox` | `mailbox_async`, `self` |
| `test_mbox_high` | `struct k_mbox` | both, `up` |
| `test_mbox_low` | `struct k_mbox` | both, `down` |
| `tx_msg`, `rx_msg` | the `struct k_mbox_msg` of sender and receiver | both |
| `message_size` | the message size of the current `step`, written into `tx_msg.size` and `rx_msg.size` | both |
| `tx_buffer`, `rx_buffer` | `uint8_t[MESSAGE_SIZE_MAX]` each, source and destination of the copy | both |

## Measurement

Triggers and marker placement are those of the family scenarios. `mailbox_sync` has no `self`
scenario and runs `up` and `down` only, each with two stop markers after one start marker: one in the
receiver after the receive operation returns, one in the sender after the send operation
returns.

| Test case | Scenario | What the window holds |
| --- | --- | --- |
| `mailbox_async` | `self` | `k_mbox_async_put()` into storage, `k_mbox_get()` out of it, one copy |
| `mailbox_async` | `up` | `k_mbox_async_put()` to the waiting receiver, one switch, `k_mbox_get()`, one copy |
| `mailbox_async` | `down` | `k_mbox_async_put()` to the waiting receiver, the sender's wait for its next start up to the point where it pends, one switch, `k_mbox_get()`, one copy |
| `mailbox_sync` | `up` | to the receiver's stop marker: `k_mbox_put()` to the waiting receiver up to the point where the sender pends, one switch, `k_mbox_get()`, one copy; to the sender's stop marker in addition the receiver's wait for its next start up to the point where it pends, one switch, the return of `k_mbox_put()` |
| `mailbox_sync` | `down` | to the sender's stop marker: `k_mbox_put()` to the waiting receiver up to the point where the sender pends, one switch, `k_mbox_get()` up to the point where it readies the sender, one switch, the return of `k_mbox_put()`, one copy; to the receiver's stop marker in addition the sender's wait for its next start up to the point where it pends, one switch, the rest of `k_mbox_get()` |

In `mailbox_async`, `down - up` is the rest and return of `k_mbox_async_put()` plus the sender's
wait for its next start up to the point where it pends, and holds no work of the storage. In
`mailbox_sync` the sender pends inside `k_mbox_put()` in both scenarios, so `down - up` at the
receiver's stop marker is the return of `k_mbox_put()`, the sender's wait for its next start up
to the point where it pends, and two switches.

| Test case | Load |
| --- | --- |
| `mailbox_async` | message size |
| `mailbox_sync` | message size |

Each copy in the table above moves the message size of the current `step`.

Scenarios are driven in the order of the general readme.

## Compensation

None.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option | Value | Meaning |
| --- | --- | --- |
| `CONFIG_NUM_MBOX_ASYNC_MSGS` | 10, the Kconfig default | `mailbox_async` builds only for a value > 0. |
