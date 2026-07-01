# Zephyr Benchmark

This repository offers a Zephyr RTOS application used to benchmark Zephyr as part of a master's thesis.
The app targets the FRDM-MCXN947 board (`frdm_mcxn947/mcxn947/cpu0`).

## Setup

Install the Toolchain needed for Zephyr [Getting
Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

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

**Option A — clone Zephyr from disk**

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


**Option B — clone Zephyr from Git** 

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

>[!NOTE] the location of ZEPHYR_BASE in this option is whereever you cloned it.

## Build, flash, debug

Make sure that `west` is recognized as command and that the build command exists.

```sh
west build -b frdm_mcxn947/mcxn947/cpu0 .
west flash --runner linkserver
```

## Debugger setup (VS Code)

In order to debug within VS code copy the [templates/](templates/) to a `.vscode`-folder.

```sh
mkdir -p .vscode
cp templates/launch.json templates/tasks.json .vscode/
```

- In `.vscode/tasks.json`, set the `linkserver` task's `command` to your
  `west.exe`. (With `zephyr.base` set via `west config`, no `ZEPHYR_BASE` env
  entry is needed.)
- In `.vscode/launch.json`, replace `<ZEPHYR_SDK_PATH>` so `gdbPath` resolves
  to `<ZEPHYR_SDK_PATH>/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb.exe`.

Then run the **Debug (LinkServer)** configuration.
