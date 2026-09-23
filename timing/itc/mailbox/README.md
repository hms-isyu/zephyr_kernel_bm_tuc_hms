# mailbox, message transfer via `k_mbox`

This directory holds two test cases, differing in the send operation. Set one of them in
`prj.conf` (Kconfig choice `BENCHMARK_TEST`, one test case per build) and rebuild:

* `CONFIG_BENCHMARK_TEST_IPC_MAILBOX_ASYNC=y` builds [mailbox_async.c](mailbox_async.c), which
  sends with `k_mbox_async_put()`. Called **asynchronous** below.
* `CONFIG_BENCHMARK_TEST_IPC_MAILBOX_SYNC=y` builds [mailbox_sync.c](mailbox_sync.c), which
  sends with `k_mbox_put()`. Called **synchronous** below.

Terminology: [../README.md](../README.md).

These test cases time one message transfer through `k_mbox`, whose send moves no payload at
all: it exchanges two descriptors, and the receiver takes the single copy inside
`k_mbox_get()`. That holds in every scenario, the self send included, where `k_msgq` and a
`k_pipe` with a ring copy twice. The payload stays in `tx_buffer`, and the storage, the send
queue of the object, holds a descriptor pointing at it rather than the message.

`k_mbox_async_put()` returns before the receiver has consumed, `k_mbox_put()` holds the caller
until it has. Holding the caller costs the synchronous build its `self` scenario.

## Participants

Tasks and priorities: [../README.md](../README.md).

| Name | What it is | Test case, scenario |
| --- | --- | --- |
| `k_mbox_async_put()`, `k_mbox_get()` | the measured operations | asynchronous |
| `k_mbox_put()`, `k_mbox_get()` | the measured operations | synchronous |
| `test_mbox_self` | `struct k_mbox` | asynchronous, `self` |
| `test_mbox_high`, `test_mbox_low` | `struct k_mbox`, one per scenario | both, `up` and `down` |
| `tx_msg`, `rx_msg` | the two `struct k_mbox_msg` the send exchanges, both open to `K_ANY` | both |
| `arm_descriptors()` | reopens both descriptors before each transaction | both |
| `message_size` | the size at the current step, read into both descriptors | both |
| `tx_buffer`, `rx_buffer` | `uint8_t[MESSAGE_SIZE_MAX]` each, source and destination of the copy | both |
| `async_msg_free` | the pool: a `k_stack` of `struct k_mbox_async`, `CONFIG_NUM_MBOX_ASYNC_MSGS` elements, which the send draws an element from | asynchronous |
| `mbox_message_dispose()` | the dispose: run by the receiver, it returns the element to the pool | asynchronous |

`mbox_message_match()` writes both thread identifiers into the descriptors, and a descriptor
left that way turns the next send into a targeted one. `arm_descriptors()` sets both back to
`K_ANY` before every transaction, which is before the start marker and outside every window.

## Measurement

| Test case | Scenario | Measurement window | What the window holds |
| --- | --- | --- | --- |
| asynchronous | `self` | both markers in `T_Self` | the send, which parks a pool element on the send queue, and the get, which matches it, copies and returns it |
| asynchronous | `up` | start in `T_SendUp`, stop in `T_RecvUp` | the send, the switch into the receiver, the copy and the dispose |
| asynchronous | `down` | start in `T_SendDown`, stop in `T_RecvDown` | the send, the sender's block, the switch, the copy and the dispose |
| synchronous | `up` | start in `T_SendUp`, `send_to_higher_prio` stopping in `T_RecvUp`, `rendezvous_to_higher_prio` stopping in `T_SendUp` | the send, which parks the sender, the switch, the copy and the dispose, closed once on each side |
| synchronous | `down` | start in `T_SendDown`, `send_to_lower_prio` stopping in `T_RecvDown`, `rendezvous_to_lower_prio` stopping in `T_SendDown` | the same, closed once on each side |

The two series of one synchronous scenario are nested, not parallel. The task left running
closes first: in `up` the receiver, so `send_to_higher_prio` closes inside
`rendezvous_to_higher_prio`; in `down` the sender, so `rendezvous_to_lower_prio` closes inside
`send_to_lower_prio`.

`asynchronous - synchronous` at the same scenario is the send, with the copy in the same place
on both sides.

One object per scenario serves every step, the size travelling in the descriptors, so nothing
is created or resized between steps.

## Analysis

Expected of every scenario: the value follows the message size, which is the length of
the one copy. Expected against [../msgq](../msgq) and the with ring [../pipe](../pipe): a
smaller dependence on the size in the self send, one copy against their two, and a comparable
one in the cross-task scenarios, where all three copy once.

The receiver is already waiting in every cross-task scenario, so the send matches on the first
entry it walks and no descriptor is ever parked. Only the asynchronous `self` scenario parks
one, and only the asynchronous test case touches the pool, once per transaction: one element
drawn by the send, one returned by the dispose. [../stack](../stack) times that same pair of
`k_stack` operations on its own.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option (`prj.conf`) | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_NUM_MBOX_ASYNC_MSGS` | 10, the Kconfig default | `k_mbox_async_put()` needs it above 0, so the asynchronous test case exists only while it stays there. One element is in use at a time. |
| `CONFIG_ASSERT_ON_ERRORS`, `CONFIG_NO_RUNTIME_CHECKS` | not set | `CHECKIF()`, from `zephyr/sys/check.h`, keeps its `if (expr)` form, so the pool return carries a real compare inside the asynchronous window. Under either symbol it leaves the measurement. |
