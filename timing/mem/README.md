### Terminology

| Term             | Meaning                                                                                                                                                                        |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| allocator        | the thing under test                                                                                                                                                           |
| arena            | the bytes the allocator was given, `MEM_ARENA_SIZE` = 4096 B                                                                                                                   |
| granularity      | the size the arena is cut at, chosen when it is created                                                                                                                        |
| allocation       | what one `mem_allocator_alloc()` returns                                                                                                                                       |
| live             | an allocation that has not been freed                                                                                                                                          |
| request          | the size in bytes asked for by one allocate                                                                                                                                    |
| step             | `s`; the request is `2^s`, s = s_min … s_max (3 … 10)                                                                                                                          |
| index            | position of an allocation in the order the clogging loop got it                                                                                                                |
| hole             | free space left by one free, between two live allocations                                                                                                                      |
| gap              | a hole whose size and physical position the setup fixed; W7 only                                                                                                               |
| reclaim          | what free does when the freed bytes reach a later allocate                                                                                                                     |
| full             | `mem_allocator_alloc()` has returned NULL                                                                                                                                      |
| served           | a step one allocate on a freshly created arena returns non-NULL for                                                                                                            |
| requested space  | the sum of requests one workload asks for, `MEM_REQUESTED_SPACE` = 2^s_max counted as `2^s` per allocation. Identical for every allocator — this is what makes runs comparable |
| total space used | the arena bytes actually consumed to serve that requested space, requests plus whatever the allocator added. Differs per allocator — a result, not an input                    |
| count            | `requested space / 2^s`; the allocations one workload touches at one step                                                                                                      |
| workload         | one row of the table, W1 to W7_C                                                                                                                                               |
| run              | one execution of one workload at one step; one sample                                                                                                                          |

Invariant: requested space ≤ total space used ≤ arena.

**Verbs**, only these five: **create** the arena at a granularity · **destroy** it · **clog** it, meaning allocate repeatedly until full · **allocate** one request · **free** one allocation.

### Constants

```
s_min = 3,  s_max = 10,  s_min < s_max        [chosen]
k     = 4                                     [arena multiplier, chosen]
```

### Symbols

| Symbol | Domain                                               | Unit    | Kind         | Meaning                                          |
| ------ | ---------------------------------------------------- | ------- | ------------ | ------------------------------------------------ |
| `s`    | ℤ, s_min ≤ s ≤ s_max                                 | —       | input        | step                                             |
| `r(s)` | `r = 2^s`                                            | Bytes   | derived      | request                                          |
| `R`    | `R = 2^s_max` = 1024                                 | Bytes   | derived      | requested space                                  |
| `A`    | `A = k·R = 2^(s_max+2)` = 4096                       | Bytes   | derived      | arena, `MEM_ARENA_SIZE`                          |
| `g`    | `g = r(s) = 2^s`                                     | Bytes   | input        | granularity, tied to the step in every workload  |
| `c(s)` | `c = R / r = 2^(s_max−s)`, c ∈ {1 … 2^(s_max−s_min)} | —       | derived      | count: allocations one workload touches          |
| `i`    | ℤ, 0 ≤ i ≤ N−1                                       | —       | index        | position in the order the clog got them          |
| `N(s)` | ℤ, N ≥ 1                                             | —       | **measured** | allocations one clog yields; allocator-dependent |
| `aᵢ`   | —                                                    | address | measured     | address returned for allocation `i`              |
| `o(s)` | `o = aᵢ₊₁ − aᵢ − r`                                  | Bytes   | **measured** | what the allocator adds per allocation           |
| `T(s)` | `R ≤ T ≤ A`                                          | Bytes   | **measured** | total space used                                 |

`N`, `o` and `T` are results, not parameters. Everything the harness sets is above them.

### Workloads

`create(g)` · `destroy` · `clog(r)` = allocate `r` until full · `allocate(r)` · `free(i)`. Destroy after every run.

| ID       | Setup (untimed)                                                                                | Measured                                                                                  | Purpose                                                                                                |
| -------- | ---------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| **W1**   | `create(g)`                                                                                   | `allocate(r)`                                                                             | allocate cost against request size, no hole present — the path that carves from untouched space        |
| **W2**   | `create(g)`; `allocate(r)` ×3 → i = 0,1,2; `free(1)`                                          | `allocate(r)`                                                                             | allocate cost with a hole of exactly `r` between two live allocations. W2 − W1 isolates the split path |
| **W3**   | `create(g)`; `allocate(r)` → i = 0                                                            | `free(0)`                                                                                 | free cost against request size, one side adjacent to free space                                        |
| **W4**   | `create(g)`                                                                                   | `allocate(r)` until Σ requests = R, or full; one window over the loop                     | aggregate allocate cost for a fixed requested space; yields `T(s)` and `o(s)`                          |
| **W4_FIXED_SIZE** | `create(g₀)`                                                                          | `mem_allocator_alloc_n(c)`, one call for the whole requested space; one window            | the same requested space through a count-taking API. Only where the allocator has one; not applicable elsewhere |
| **W5**   | `create(g)`; `clog(r)`                                                                        | `free(i)`, i = 1, 3 … 2c−1, ascending                                                     | free cost against hole count, both neighbours live                                                     |
| **W6**   | `create(g)`; `clog(r)`; `free(i)`, i = 0, 2 … 2c                                              | `free(i)`, i = 1, 3 … 2c−1, ascending                                                     | free cost when both neighbours are holes — the reclaim path                                            |
| **W7_A** | `create(g)`; `clog` with layout L; `free` in order 32_before, >256, 128, 32_after1, 32_after2 | `allocate(128)`; record cycles and `a`, classified GAP256 / GAP128 / GAP32 / NULL / OTHER | which gap the policy selects with the 128 B gap freed last                                             |
| **W7_B** | as W7_A, free order 32_before, 128, >256, 32_after1, 32_after2                                 | `allocate(128)`; classification only                                                      | a differing result proves traversal follows free order                                                 |
| **W7_C** | as W7_A, then `allocate(128)` and `free` that allocation                                       | `allocate(128)`; classification only                                                      | whether the search restarts at the head or resumes                                                     |
| **W8**   | — (destroy any prior arena)                                                                    | `create(g)`; sweep `s = 2^s`,s = s_min … s_max, one run per s                             | arena creation cost against granularity. Expect ∝ A/g for pool and bitmap, flat for list and bump      |


All workloads sweep `s` over its full domain, one run per step. **L**: a predetermined sequence of 16 B and 32 B requests that clogs the arena and places, in address order, one 32 B allocation, one larger than 256 B, one of exactly 128 B, then two 32 B allocations.
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
