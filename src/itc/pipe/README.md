# pipe, one transaction through `k_pipe`

This directory holds two test cases. Set one of the symbols and rebuild:

* `CONFIG_BENCHMARK_TEST_ITC_PIPE_ASYNC=y` selects test case `pipe_async`.
* `CONFIG_BENCHMARK_TEST_ITC_PIPE_SYNC=y` selects test case `pipe_sync`.

Family and terms: [../README.md](../README.md).

`k_pipe` passes a stream of bytes from a sender to a receiver and holds the bytes no receiver
waits for in its storage, as far as the storage has room. These test cases time one transaction
through it.

## Participants

| Name | What it is | Test case, scenario |
| --- | --- | --- |
| `k_pipe_write()`, `k_pipe_read()` | send operation, receive operation | both |
| `T_Self` | sender and receiver; holds the message size in a local variable set before the transaction loop | `self` |
| `T_SendUp`, `T_RecvUp` | sender, receiver; `receiver > sender` | `up` |
| `T_SendDown`, `T_RecvDown` | sender, receiver; `sender > receiver` | `down` |
| `T_ArmUp` | control task; `sender > control task`, `receiver > control task` | `up` |
| `T_ArmDown` | control task; `sender > control task`, `receiver > control task` | `down` |
| `test_pipe_self` | `struct k_pipe` | `pipe_async`, `self` |
| `test_pipe_high` | `struct k_pipe` | `pipe_async`, `up` |
| `test_pipe_low` | `struct k_pipe` | `pipe_async`, `down` |
| `pipe_buffer_self`, `pipe_buffer_high`, `pipe_buffer_low` | the storage of the object of the same suffix | `pipe_async` |
| `test_pipe` | `struct k_pipe`, initialised before `up` and again before `down` | `pipe_sync` |
| `active_message_size` | the message size of the current `step`, set by the control task before the first transaction of the step; the receiver reads it before it pends, the sender after its start marker, one read inside the window | both, `up` and `down` |
| `tx_buffer`, `rx_buffer` | `uint8_t[MESSAGE_SIZE_MAX]` each, source and destination of the copy | both |

## Measurement

Triggers and marker placement are those of the family scenarios. `pipe_sync` runs `up` and
`down` only, each with two stop markers after one start marker: one in the receiver after the
receive operation returns, one in the sender after the send operation returns.

| Test case | Scenario | What the window holds |
| --- | --- | --- |
| `pipe_async` | `self` | `k_pipe_write()` into storage, which has room for the message, `k_pipe_read()` out of it, two copies |
| `pipe_async` | `up` | `k_pipe_write()` to the waiting receiver, which takes all of it, one copy, one switch, `k_pipe_read()` |
| `pipe_async` | `down` | `k_pipe_write()` to the waiting receiver, which takes all of it, one copy, the sender's wait for its next start up to the point where it pends, one switch, `k_pipe_read()` |
| `pipe_sync` | `up` | to the receiver's stop marker: `k_pipe_write()` to the waiting receiver, which takes all of it, one copy, one switch, `k_pipe_read()`; to the sender's stop marker in addition the receiver's wait for its next start up to the point where it pends, one switch, the return of `k_pipe_write()` |
| `pipe_sync` | `down` | to the sender's stop marker: `k_pipe_write()` to the waiting receiver, which takes all of it, one copy, and its return, no switch; to the receiver's stop marker in addition the sender's wait for its next start up to the point where it pends, one switch, `k_pipe_read()` |

At the receiver's stop marker, `down - up` is the rest and return of `k_pipe_write()` plus the
sender's wait for its next start up to the point where it pends, and holds no work of the
storage. `self - up` holds the storage path of both operations and no switch where `up` holds the
waiting receiver path and one switch, so it never isolates the storage.

| Test case | Storage size | Load |
| --- | --- | --- |
| `pipe_async` | `PIPE_BUFFER_SIZE` = `MESSAGE_SIZE_MAX` | message size |
| `pipe_sync` | 0 | message size |

Each copy named in the table above moves the message size of the current `step`. In `self`, `test_pipe_self` is
not reset between steps, so the number of pieces each copy takes is not fixed across `step`.

Scenarios are driven in the order of the general readme.

## Compensation

None.

## Configuration

Shared configuration: [../README.md](../README.md).
