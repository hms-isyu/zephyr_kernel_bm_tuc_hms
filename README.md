# Zephyr Benchmark

This repository offers a Zephyr RTOS application used to benchmark Zephyr as part of a master's thesis.
The app targets the FRDM-MCXN947 board (`frdm_mcxn947/mcxn947/cpu0`).

## Setup

Install the host tools needed for Zephyr (west, Python, CMake, Ninja, dtc,
gperf) by following the Zephyr [Getting Started
Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

> [!CAUTION]
> Do not proceed to "west init" Zephyr!

> [!IMPORTANT]
> **The compiler is not the Zephyr SDK.** This application builds with
> `ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb` against an external [Arm GNU
> Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
> (`arm-none-eabi`, 14.2.rel1 or compatible) for comparative research.

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
