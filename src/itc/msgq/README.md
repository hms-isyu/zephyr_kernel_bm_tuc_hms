# msgq, one transaction through `k_msgq`

Set `CONFIG_BENCHMARK_TEST_ITC_MSGQ=y` and rebuild.
Family and terms: [../README.md](../README.md).

`k_msgq` copies a message in on the send operation and copies it out on the receive operation.
This test case times one transaction through it.

## Participants

| Name | What it is | Scenario |
| --- | --- | --- |
| `k_msgq_put()`, `k_msgq_get()` | send operation, receive operation | all |
| `T_Self` | sender and receiver | `self` |
| `T_SendUp`, `T_RecvUp` | sender, receiver; `receiver > sender` | `up` |
| `T_SendDown`, `T_RecvDown` | sender, receiver; `sender > receiver` | `down` |
| `T_ArmUp` | control task; `sender > control task`, `receiver > control task` | `up` |
| `T_ArmDown` | control task; `sender > control task`, `receiver > control task` | `down` |
| `test_msgq_self[step]` | `struct k_msgq`, one per `step` | `self` |
| `test_msgq_high[step]` | `struct k_msgq`, one per `step` | `up` |
| `test_msgq_low[step]` | `struct k_msgq`, one per `step` | `down` |
| `msg_size`, `max_msgs` | per object: the message size at its `step`, and `MSGQ_MAX_MSGS` = 1 | all |
| `msgq_buffer_self[step]`, `msgq_buffer_high[step]`, `msgq_buffer_low[step]` | `MESSAGE_SIZE_MAX * MSGQ_MAX_MSGS` bytes per object of the same suffix, the storage behind it | `self`, `up`, `down` |
| `active_msgq` | the object of the current `step`, set by the control task before the first transaction of the step | `up`, `down` |
| `tx_buffer`, `rx_buffer` | `uint8_t[MESSAGE_SIZE_MAX]` each, source and destination of the copy | all |

## Measurement

Triggers and marker placement are those of the family scenarios.

| Scenario | What the window holds |
| --- | --- |
| `self` | `k_msgq_put()` into storage, `k_msgq_get()` out of it, two copies |
| `up` | `k_msgq_put()` to the waiting receiver, one switch, `k_msgq_get()`, one copy |
| `down` | `k_msgq_put()` to the waiting receiver, the sender's wait for its next start up to its pend, one switch, `k_msgq_get()`, one copy |

`down - up` is the rest and return of `k_msgq_put()` plus the sender's wait for its next start up
to its pend, since both windows otherwise share the same `k_msgq_put()` up to readying the
receiver, one switch, and the same rest of `k_msgq_get()`, and holds no work of the storage.
`self - up` holds the storage path of both operations and no switch where `up` holds the waiting
receiver path and one switch, so `self - up` never isolates the storage.

| Test case | Load |
| --- | --- |
| `msgq` | message size |

Each copy counted above moves the message size of the current `step`.

Scenarios are driven in the order of the general readme.

## Compensation

None.

## Configuration

Shared configuration: [../README.md](../README.md).
