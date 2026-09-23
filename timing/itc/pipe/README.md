# pipe, message transfer via `k_pipe`

This directory holds two test cases, differing in one argument of `k_pipe_init()`. Set one of
them in `prj.conf` (Kconfig choice `BENCHMARK_TEST`, one test case per build) and rebuild:

* `CONFIG_BENCHMARK_TEST_IPC_PIPE_ASYNC=y` builds [pipe_async.c](pipe_async.c), whose pipes
  have a ring of `PIPE_BUFFER_SIZE` bytes. Called **with ring** below.
* `CONFIG_BENCHMARK_TEST_IPC_PIPE_SYNC=y` builds [pipe_sync.c](pipe_sync.c), whose pipe has a
  NULL buffer and size 0. Called **no ring** below.

Terminology: [../README.md](../README.md).

These test cases time one message transfer through `k_pipe`, which copies the bytes. The
storage is a byte ring. With a reader waiting the message is copied once, into that reader's
own stack buffer; with none the write fills the ring and the read empties it, twice. The
length is an argument of each call, where `k_msgq` fixes it when the object is created, so one
object serves every size.

Without a ring a write has nowhere to put bytes and completes only into a waiting reader. That
costs the no ring build its `self` scenario, which needs a write that completes with no
reader.

## Participants

Tasks and priorities: [../README.md](../README.md). `T_Send*` is the writer, `T_Recv*` the
reader.

| Name | What it is | Test case, scenario |
| --- | --- | --- |
| `k_pipe_write()`, `k_pipe_read()` | the measured operations | both |
| `test_pipe_self`, `test_pipe_high`, `test_pipe_low` | `struct k_pipe`, one per scenario, ring of `PIPE_BUFFER_SIZE` = `MESSAGE_SIZE_MAX` bytes | with ring |
| `pipe_buffer_self`, `pipe_buffer_high`, `pipe_buffer_low` | the ring of each object | with ring |
| `test_pipe` | `struct k_pipe` with no ring, one object for both scenarios, created again between them | no ring |
| `active_message_size` | carries the length of the step to writer and reader | both, `up` and `down` |
| `tx_buffer`, `rx_buffer` | `uint8_t[MESSAGE_SIZE_MAX]` each, source and destination of the copy | both |

The length is an argument, so writer and reader read `active_message_size` before they enter
their call, which is before the start marker and outside every window.

## Measurement

| Test case | Scenario | Measurement window | What the window holds | Copies |
| --- | --- | --- | --- | --- |
| with ring | `self` | both markers in `T_Self` | the write and the read, through the ring | 2 |
| with ring | `up` | start in `T_SendUp`, stop in `T_RecvUp` | the write, the switch into the reader, the rest of the read | 1 |
| with ring | `down` | start in `T_SendDown`, stop in `T_RecvDown` | the write, the writer's block, the switch, the rest of the read | 1 |
| no ring | `up` | start in `T_SendUp`, `send_to_higher_prio` stopping in `T_RecvUp`, `rendezvous_to_higher_prio` stopping in `T_SendUp` | the same, closed once on each side | 1 |
| no ring | `down` | start in `T_SendDown`, `send_to_lower_prio` stopping in `T_RecvDown`, `rendezvous_to_lower_prio` stopping in `T_SendDown` | the same, closed once on each side | 1 |

The two series of one no ring scenario are nested, not parallel. The task left running closes
first: in `up` the reader, so `send_to_higher_prio` closes inside `rendezvous_to_higher_prio`;
in `down` the writer, so `rendezvous_to_lower_prio` closes inside `send_to_lower_prio`.

`with ring - no ring` at the same scenario is the storage, both sides copying once into the
waiting reader. `self - up` is not, because `up` carries the switch as well.

`PIPE_BUFFER_SIZE` is not swept, and every step size divides it, so no message straddles the
wrap at any step. With ring, the driver empties the pipe after each window closes.

## Analysis

Expected of every scenario: the value follows the message size, which is the length of
every copy. Expected of the with ring `self` scenario: the larger dependence on the size of the
three, moving the message twice where the cross-task scenarios move it once.

At the top step of that scenario the message is exactly `PIPE_BUFFER_SIZE`, and the read is
entered on a full ring where every lower step enters it on a partly filled one. That step is
not on the same path as the others, and a step to step comparison across it carries the
difference.

The reader is inside the read before the write starts, so the write never waits for room in
either test case. The waiting path of the write is not covered.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option (`prj.conf`) | Value | Consequence special to this test |
| --- | --- | --- |
| `CONFIG_KERNEL_COHERENCE` | not set | `k_pipe_write()` keeps `copy_to_pending_readers()`, the copy straight into the waiting reader, which is the measured path of every cross-task scenario here. The symbol guards that call: set, the write wakes the readers and routes the bytes through the ring instead, which the no ring test case does not have, so `with ring - no ring` would stop being a storage comparison. |
