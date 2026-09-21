# MEM ALLOCATION BENCHMARK
- the measurement of allocation perfomance is complex because of the different approaches of allocating memory.
- there are generally also other aspects that may count as performance relevant, specially wait queues, when allocation is not possible. This however is an issue for a separate test harness.
- The test harness must cover different axis of allocation algorithm:
	- allocation performance
		- fit policy with list ordering
	- free performance
		- without coalescing
		- with coalescing
	- reset/creation of an area


## Setup

s: step

Defaults:

s_min = 3
s_max = 10
Arena_size = 4 * s_max
definition: usable memory is the allocation of memory that will be occupied by application data and not by headers.

| ID       | Name                        | Description                                                                               | Setup (untimed)                                                                                              | Measurement                                                                                                        |
| -------- | --------------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------ |
| **W1**   | alloc by size, empty        | allocation cost against request size, arena holding one free chunk                        | `create` or `reset`                                                                                          | `ALLOC 2^s`; sweep: s = s_min … s_max; one run per s; NULL recorded as a result                                    |
| W2       | alloc by size, 2 neighboors | allocation cost against request size, arena 2 chunks where there is one chunk gap between | create or reset, ALLOC 3 2^s chunks, free the chunk in the middle                                            | `ALLOC 2^s`; sweep: s = s_min … s_max; one run per s; NULL recorded as a result                                    |
| **W3**   | free, one neighbour         | free cost against request size, exactly one coalesce branch taken                         | `create` or `reset`; one `ALLOC 2^s`; sweep: s = s_min … s_max; steps where it returns NULL are recorded N/A | `FREE` that chunk                                                                                                  |
| **W4**   | time to fill the arena      | aggregate allocation cost until the arena is exhausted                                    | `create` or `reset`                                                                                          | `ALLOC 2^s` until 2^s_max of usable memory was allocated. One window over the whole loop; sweep: s = s_min … s_max |
| **W5**   | free, no coalesce           | free cost against the number of holes already on the free list, neither neighbour free    | `create` or `reset`; `ALLOC 2^s` until exhausted; sweep: s = s_min … s_max−2                                 | `FREE` the odd-indexed chunks, ascending index, but only inside the range of s_max                                 |
| **W6**   | free, both neighbours       | free cost with both coalesce branches taken, against the sweep index                      | `create` or `reset`; `ALLOC 2^s` until exhausted; sweep: s = s_min … s_max−2; `FREE` the even-indexed chunks | `FREE` the odd-indexed chunks, ascending index, but only inside the range of s_max                                 |
| **W7**   | arena creation              | arena creation cost against the granularity it is cut at                                  | `reset`                                                                                                      | `create g` the arena; sweep: g = s_min … s_max                                                                     |
| **W8_A** | fit policy, free order 1    | which hole the policy selects when free order puts the 128 B gap at the list head         | layout L; `FREE` the five in order 32_before, >256, 128, 32_after1, 32_after2                                | `ALLOC 128 B`; record cycles **and** the returned address, classified GAP256 / GAP128 / DECOY32 / NULL / OTHER     |
| **W8_B** | fit policy, free order 2    | same layout, > 256 gap at the list head instead                                           | layout L; `FREE` in order 32_before, 128, >256, 32_after1, 32_after2                                         | `ALLOC 128 B`; address and classification only, no cycles                                                          |
| **W8_C** | next-fit probe              | whether the search resumes from a roving pointer or restarts at the head                  | layout L, W7_A's free order; then untimed `ALLOC 128 B` and `FREE` of that same pointer                      | `ALLOC 128 B`; address and classification only, no cycles                                                          |

Series shapes: W1, W2, W3, W6 → `[8]` indexed `s−3` (or `g−3`); W4, W5 → `[6]` indexed `s−3`, each buffered over the sweep index; W7_A → one. W7_B and W7_C have no series.

**`w7_guess`**, derived once across the three W7 runs, in this order:

|W7_A|W7_B|W7_C|`w7_guess`|
|---|---|---|---|
|DECOY32 in any run|-|-|`BUG` — served a 128 B request from a 32 B hole|
|NULL|-|-|`REFUSED` — no coalescing, or piece size too large (pool)|
|differs from W7_B|-|-|`LIST_ORDERED` — insertion-ordered (LIFO) with an early-exit fit|
|GAP128|GAP128|-|`BEST_FIT` — or first-fit on a size-ordered list|
|GAP256|GAP256|GAP256|`FIRST_FIT` — address-ordered|
|GAP256|GAP256|GAP128|`NEXT_FIT` — address-ordered|
|all ran, no row matched|||`OTHER`|
|not all three ran|||`UNKNOWN`|
