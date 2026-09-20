# Create a Customized Version

Flight controllers have limited flash and RAM, so the features built into the official firmware
are a compromise. If you build your own firmware you can switch features on and off to save space
or to enable something the official build leaves out.

Keep in mind that a firmware you build yourself is a piece of software that you are responsible
for. It can have bugs that the official version does not, and enabling a feature that the
developers left off may overload the processor or overflow flash or RAM. Do not fly it before you
have tested it on the bench.

This is for people who can already build the firmware from source. See [Building the Firmware](https://doc.wingflight.org/contributing/building-the-firmware/) for the toolchain and
targets, and [Development](Development.md) for contribution guidelines. Choose a unified target that matches your MCU:

```
make TARGET=STM32F405
```

The build ends with a size summary:

```
   text    data     bss     dec     hex filename
```

`text + data` is the flash used, and `data + bss` is the static RAM used. Keep them under the
flash and RAM size of your MCU.

## Where features are chosen

* `src/main/target/common_pre.h` sets the features that are on or off for all targets, and often
  depends on the flash size of the MCU. This is the first place to look.
* `src/main/target/common_post.h` and `common_deprecated_post.h` derive dependent settings after
  the target file has been read.
* The unified target's own `target.h` (in `src/main/target/STM32_UNIFIED/`) covers what is specific
  to an MCU family.

A feature is a `USE_` macro. To remove a feature you do not use, such as a receiver protocol,
telemetry protocol or VTX protocol, remove or `#undef` its `USE_` macro, and rebuild to see how
much flash it saves.

You can also pass a macro on the command line without editing the files:

```
make TARGET=STM32F405 OPTIONS="USE_SOMETHING"
```

## Tips

* Change one thing at a time and rebuild. Some features depend on others, and the build will tell
  you when something you removed is still in use.
* Removing a receiver or telemetry protocol you never use is the safest way to gain space.
* If you enable a feature that is commented out or disabled by default, keep an eye on the CPU
  load, which you can see with the CLI `tasks` command.
