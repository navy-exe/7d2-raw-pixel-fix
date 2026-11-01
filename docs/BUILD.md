# Build and analysis

The source snapshot is based on the upstream commit recorded in
`environment.json`. It includes the current60s experiment. It is not a
ready-to-install package, and no boot-flag/firmware installation is automated.

The local tested toolchain was xPack ARM GNU GCC13.3.1-1.1, GNU make/MSYS.
Follow the upstream build documentation for platform dependencies. From
`source/magiclantern_simplified`, using your compiler's absolute bin directory:

```sh
make -C platform/7D2.112 ARM_BINPATH=/absolute/toolchain/bin FATAL_WARNINGS=y CONFIG_QEMU=n
make -C modules/raw_video/mlv_lite ARM_BINPATH=/absolute/toolchain/bin FATAL_WARNINGS=y CONFIG_QEMU=n
make -C modules/dev_tools/rwtap ARM_BINPATH=/absolute/toolchain/bin FATAL_WARNINGS=y CONFIG_QEMU=n
```

The source requires the corresponding Magic Lantern core/exported symbols;
binary identity/build context from the tested camera is recorded separately.
A successful rebuild alone does not establish compatibility with another core,
camera or firmware. Review the integration notes before any camera experiment.

For the portable host lifecycle check, from the repository root:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror tools/test_capture_tap_video.c -o /tmp/test_capture_tap
/tmp/test_capture_tap
```

The test includes the actual lifecycle helper with mocked hardware callbacks.
Historical compiled ARM and native emulation results are under `evidence/`.

Analysis requires Python3, NumPy and Pillow. Given your own original data:

```sh
python tools/analyze_raw_tap.py /path/to/TP0001.TST
python tools/inspect_mlv_capture.py /path/to/clip.MLV /path/to/inspection-output
```

The tap analyzer also needs the corresponding RAW/HDR pairs. These personal
payloads are not included. The MLV inspector supports the tested plain14-bit
format, not a universal compressed/split-file validator. Keep original files
read-only and assess new formats/modes independently.

## Source patches

The complete source already has the changes. To apply patches elsewhere:

- `upstream-to-working.patch`: start from the specified clean upstream commit.
- `capture-point-only.patch`: start from the exact prior recorder in `baseline/`.

Do not stack these two patches. Both patch application paths are verified during
publication preparation against their respective baselines.
