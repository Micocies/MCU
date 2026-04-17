# Tests

This directory contains the minimal V0.1 host test harness.

## Host Tests

Build and run from the repository root:

```sh
cmake -S tests -B build/tests
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

You can also run the executable directly:

```sh
./build/tests/host_tests
```

On Windows generators the executable may be under a configuration directory, for example:

```sh
./build/tests/Debug/host_tests.exe
```

## ARM Smoke Check

The host tests are not intended to run on the MCU. For the embedded toolchain,
use the existing firmware project as the source of truth and do a compile/link
smoke build with `arm-none-eabi-gcc` or the configured IDE toolchain after host
tests pass. The goal is to verify that `UNIT_TEST` conditionals are absent from
the normal build and that the production headers still compile for STM32G431.

Recommended minimum check:

1. Build the normal firmware target without `UNIT_TEST`.
2. Confirm `Core/Src/adc_protocol.c`, `Core/Src/usb_stream.c`, and `Core/Src/app.c`
   compile and link with the real HAL, USB, startup, and linker script.
3. Run a board smoke test with `tools/usb_capture.py` for packet framing and
   sample sequence continuity.

If you want a compile-only probe before a full firmware build, use the normal
project include paths and compile the touched production files without
`UNIT_TEST`, for example:

```sh
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -DSTM32G431xx \
  -ICore/Inc \
  -IDrivers/STM32G4xx_HAL_Driver/Inc \
  -IDrivers/CMSIS/Device/ST/STM32G4xx/Include \
  -IDrivers/CMSIS/Include \
  -IUSB_Device/App \
  -IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc \
  -IMiddlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc \
  -c Core/Src/adc_protocol.c Core/Src/usb_stream.c Core/Src/app.c
```

This is only a smoke check. A full firmware link remains the authoritative ARM
validation because `app.c` depends on board-level objects, interrupt startup,
HAL, USB CDC, and the linker script.
