# Building the firmware

## Requirements

* `git` and `make`.
* The ARM GNU toolchain. The build installs the version it expects with:

  ```
  make arm_sdk_install
  ```

  This puts the compiler under `tools/`. You can also set up your own toolchain, but the Makefile
  checks the compiler version and tells you if it is not the one it expects.
* On Windows, the easiest route is WSL2 with Ubuntu, using the Linux steps below. For the SITL
  target, `make mingw_sdk_install` installs a native MinGW-w64 compiler (see
  `src/main/target/SITL/README.md`).

## Get the source

```
git clone https://github.com/WingFlight/wingflight-firmware.git
cd wingflight-firmware
```

## Build

Build one unified target:

```
make TARGET=STM32F405
```

The unified targets are `STM32F405`, `STM32F411`, `STM32F7X2`, `STM32F745`, `STM32G47X` and
`STM32H743`. See [Boards](../Boards.md). To build all of them:

```
make unified
```

`make help` lists the options, and `make targets` lists the targets. Extra defines can be added
with `OPTIONS="USE_SOMETHING"`. See [Customized Version](../Customized%20Version.md).

The output is written to `obj/`, for example `obj/wingflight_<version>_STM32F405.hex`. Flash it with
the [Wingflight Configurator](https://github.com/WingFlight/wingflight-configurator/releases).

## Simulator (SITL)

```
make TARGET=SITL
```

builds a native executable. See `src/main/target/SITL/README.md`.

## Tests

```
make test
```

builds and runs the unit tests in `src/test/unit`.

## Docker

The repository has a `Dockerfile` and `docker-compose.yml` that give a clean Linux build
environment, if you do not want to install the toolchain locally.

## Continuous integration

Pull requests and pushes to `master` are built by the GitHub Actions workflows in
`.github/workflows`.
