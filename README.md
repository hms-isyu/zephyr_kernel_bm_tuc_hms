# Zephyr Benchmark

This repository offers a Zephyr RTOS application used to benchmark Zephyr as part of a master's thesis.
The app targets the FRDM-MCXN947 board (`frdm_mcxn947/mcxn947/cpu0`).

## Setup

Install the host tools needed for Zephyr (west, Python, CMake, Ninja, dtc,
gperf) by following the Zephyr [Getting Started
Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

> [!CAUTION]
> Do not proceed to "west init" Zephyr!

- Clone the repository inside a proper workspace folder:

> [!CAUTION]
> The workspace folder must be empty and cannot have other west projects in it.

```sh
git clone <this-repo-url> zephyr_benchmark
cd zephyr_benchmark
```

- Start the virtual environment as described in the Zephyr [Getting
Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

Follow one of these options:

**Option A - clone Zephyr from disk**

If you have an existing checkout and want to avoid re-downloading, seed it.
`--path-cache` takes the **parent** directory that *contains* the `zephyr`
folder (i.e. the west workspace/topdir), not `ZEPHYR_BASE` itself:

```sh
cd <this-repo>
west init -l .
west update --path-cache <ZEPHYR_BASE_PARENT>
```

> [!NOTE]
> `--path-cache` tells west to pull git objects from a local checkout instead
> of GitHub, so no download happens. It only reads that folder; the pinned
> revision from `west.yml` is checked out into a fresh `zephyr/` in this
> workspace. Cache hits require that the local checkout already has the pinned
> revision's objects.

> [!NOTE]
> The location of ZEPHYR_BASE in this option is '../zephyr'


**Option B - clone Zephyr from Git** 

```sh
cd <this-repo>
west init -l .
west update
```

> [!NOTE] 
> the location of ZEPHYR_BASE in this option is '../zephyr'

**Option C - use Zephyr from disk**

- Make sure that Zephyr has the right version.

```sh
cd <ZEPHYR_BASE> 
git fetch --tags
git checkout <project-version>
cd ..
west update
```

```sh
cd <this-repo>
west init -l .
west config manifest.path <ZEPHYR_BASE>
```

> [!NOTE] 
> the location of ZEPHYR_BASE in this option is whereever you cloned it.

## Configure the preset

Before the first build, edit the `build` preset in
[CMakePresets.json](CMakePresets.json) and
[mcux_include.json](mcux_include.json) to match your machine:

| Setting | In | What it must point at |
|---|---|---|
| `GNUARMEMB_TOOLCHAIN_PATH` | `CMakePresets.json` | your Arm GNU Toolchain root (the folder containing `bin/arm-none-eabi-gcc`) |
| `ZEPHYR_BASE` | both files | `<workspace>/zephyr` |
| `MCUX_VENV_PATH` | `mcux_include.json` | your venv's `Scripts` (or `bin`) folder |

The preset pins the board (`frdm_mcxn947/mcxn947/cpu0`), the generator (Ninja),
`app.overlay`, and the build tree, which is `build/` beside this file.

> [!IMPORTANT]
> **The compiler is not the Zephyr SDK.** This application builds with
> `ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb` against an external [Arm GNU
> Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
> (`arm-none-eabi`, 14.2.rel1 or compatible) for comparative research.

## Build, flash, debug

```sh
cmake --preset build
cmake --build build
west flash --runner linkserver
```

In VS Code the same preset is driven by the **CMake: configure** and
**CMake: build** tasks.

`west build -b frdm_mcxn947/mcxn947/cpu0 .` also works, but only if you set
`ZEPHYR_TOOLCHAIN_VARIANT` and `GNUARMEMB_TOOLCHAIN_PATH` in the environment
yourself. It does not read `CMakePresets.json`, so without them west falls back
to whatever toolchain it can find, which is usually a Zephyr SDK and produces a
different binary.

## Selecting a test case

Exactly one benchmark test is linked into the image. It is chosen by a single
`CONFIG_BENCHMARK_TEST_*` symbol in [prj.conf](prj.conf); see
[Kconfig](Kconfig) for the full list. Change that symbol and rebuild. No
reconfigure is needed, Ninja re-runs CMake by itself.

## Debugger setup (VS Code)

In order to debug within VS code copy the [templates/](templates/) to a `.vscode`-folder.

```sh
mkdir -p .vscode
cp templates/launch.json templates/tasks.json .vscode/
```

- In `.vscode/tasks.json`, set the `linkserver` task's `command` to your
  `west.exe`. (With `zephyr.base` set via `west config`, no `ZEPHYR_BASE` env
  entry is needed.)
- In `.vscode/launch.json`, replace the `gdbPath` placeholder with the
  debugger from the **same toolchain that built the ELF**, that is
  `<GNUARMEMB_TOOLCHAIN_PATH>/bin/arm-none-eabi-gdb.exe`.
  If you instead use a Zephyr SDK's `arm-zephyr-eabi-gdb.exe`, note that it
  lives under `<sdk root>/gnu/arm-zephyr-eabi/bin/`, not directly under the
  SDK root.

Then run the **Debug (LinkServer)** configuration.

## Benchmark

The benchmark measures four test categories. Each category has its own
readme with the test cases it contains.

| Category | Directory | What is measured |
|---|---|---|
| Task switching time | [src/task_switching/](src/task_switching/README.md) | Cycle-accurate task switch time, compensated for the kernel primitive that issues the switch. |
| Inter task communication | [src/itc/](src/itc/README.md) | ITC primitives, in cycles. |
| Interrupt response time | [src/irt/](src/irt/README.md) | Interrupt delay, and interrupt response time (interrupt delay with a task switch). |
| Memory allocation | [src/mem/](src/mem/README.md) | Memory allocation primitives, in cycles. |

All tests have an own specified load, which is added to the measurement. This makes it possible to do a parametrization of the resulting sweep (base measurement + load(n)) curve.

### Measurement tool

The measurements use the package **Benchmark & Measurement Tools HMS
(BMTH)**. West fetches it into
[packages/benchmark_hms/](packages/benchmark_hms/).

### Terminology

| Term | Definition |
|---|---|
| Measurement window | Code path that is measured. The window is opened and closed, so the measured path can be set in different ways. |
| Time marker (start, stop) | Point where a time stamp is obtained. |
| Measurement iteration | One measurement inside an open window. A window can run multiple iterations. |
| Measurement series | Result of multiple iterations. |
| Base measurement | The measurement of the measurement window with no load. |
| Load | A specific load added to the measurement path.|
| Prologue, tail | The part of a call before the point at which it hands off the CPU or would hand it off, and the part from that point to its return. |
| Compensation | A prologue or tail that cannot be timed inside the measurement window, because a time marker would have to sit inside the task switch. It is simulated without the switch and recorded as its own measurement series. |
| Probe | A thing being measured. |
| Workload | Usually refers to the measurement path. The work (the value, usually time) it needs. |

Per series, the iterations give these results:

| Result | Description |
|---|---|
| Accumulated time | Sum of the measured time of all iterations, in one variable. |
| Minimum, maximum | Lowest and highest measured value. |
| Jitter | Iterations where jitter was encountered. In a 100% deterministic measurement there is ideally no jitter. |
| Marker compensation | Time marker overhead, subtracted from the measured time. |

The series can also record into a user specific buffer, if needed.

### Measurement procedure

The measurement is done as following:
  
  1. Load the image via LinkSever to RAM.
  2. The debugger pauses it on main.
  3. Deattach debugger by hitting stop.
  4. Reattach after measurement is done (shown by green/red LED blinking). 

### Configuration

| Setting | Value | Meaning | Reason |
| --- | --- | --- | --- |
| `CONFIG_FPU` | n | the FPU is not used | With the FPU on, the task switch can also save and restore FPU registers. The switch time would then depend on whether a task used the FPU. |
| `CONFIG_ARM_MPU` | n | no memory protection unit regions | Zephyr can reprogram MPU regions on each task switch. These cycles are MPU configuration, not scheduling. |
| `CONFIG_CACHE` | n | no external cache controller driver | Cache hits and misses depend on what ran before, so timing would change between iterations. The image runs from SRAM, so no cached fetch path is needed. |
| `CONFIG_HW_STACK_PROTECTION` | n | no hardware stack overflow guard | On ARMv8-M the stack guard (PSPLIM or an MPU guard region) is rewritten on every task switch. That adds cycles that are not part of the switch itself. |
| `CONFIG_TRUSTED_EXECUTION_SECURE` | n | the image is not built as TrustZone secure firmware | No Secure/Non-secure transitions and no SAU setup. The image runs as one security domain, so no TrustZone code is in the measured path. |
| `CONFIG_XIP` | n | the image runs from RAM, not in place from flash | Running from flash adds wait states and prefetch effects that make fetch time vary. SRAM gives a constant fetch time. This ties to the SRAM0 line at the end of the section. |
| `CONFIG_MP_MAX_NUM_CPUS` | 1 | one CPU, exactly one task runs | The second M33 core of the MCXN947 is not used. Out of scope. |
| `CONFIG_FORCE_NO_ASSERT` | y | the `__ASSERT` statements in the measured operations expand to nothing | Assert checks add branches to the measured kernel operations. A release build does not have them. |
| `CONFIG_DEBUG` | n | Build a kernel suitable for debugging.| Not needed. |
| `CONFIG_TIMESLICING` | n | tasks of equal priority change only on an explicit call with the n | Interrupts are disabled. |
| `CONFIG_TICKLESS_KERNEL` | y | no periodic tick interrupt, the timer interrupt fires only for the next timeout | No periodic timer interrupt can fall inside a measurement window. |
| `CONFIG_SYS_CLOCK_TICKS_PER_SEC` | 100 | one tick is 10 ms | If not tickless, the interruption shall be delayed as much as possible. |
| `CONFIG_EVENTS` | y | the `k_event` kernel objects are available | Used widely. |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | 16 | priorities `0..15`, all preemptible, the kernel idle task at 16 | Tests run with at most 16 priorities. |
| `CONFIG_SCHED_MULTIQ` | n | the ready queue is one linked list (`CONFIG_SCHED_SIMPLE`) | Used for tests (tests ran in SMP and multiq). |
| `CONFIG_NEWLIB_LIBC` | y | the C library is newlib | The tests are run in a similar manner for other RTOSes for comparison. In the master's thesis the compared RTOS uses LIBC. |
| `CONFIG_NEWLIB_LIBC_NANO` | y | the nano variant of newlib | -- |
| `CONFIG_DEBUG_OPTIMIZATIONS` | y | `-Og` for every test case | Chosen because compensation methods are then easier to create. `-O2` is still usable but slightly falsifies the compensations. |

Besides that console, UART and every peripheral that is not used was turned off.

For all tests the sys_tick is disabled during runtime.

All images (code, data and task stacks) ared  linked into SRAM0 (0x30000000, secure alias of 0x20000000), which the M33 accesses over the S-bus.

## Licence

BSD-3-Clause, © 2026 HMS Industrial Networks GmbH & Co. KG.
