# V0.2 Fault Injection Test Record

日期：2026-04-17

环境：

- Host compiler: `gcc.exe` from `C:\Strawberry\c\bin`
- Build command: manual gcc command equivalent to `tests/CMakeLists.txt` because `cmake` is not in PATH
- Test binary: `build/tests/host_tests.exe`

Command:

```powershell
gcc -DUNIT_TEST -Wall -Wextra -Werror -Itests/host/include -ICore/Inc Core/Src/adc_protocol.c Core/Src/diag.c Core/Src/fault_policy.c Core/Src/usb_stream.c Core/Src/app.c tests/host/main.c tests/host/test_adc_protocol.c tests/host/test_usb_stream.c tests/host/test_app_smoke.c tests/host/stubs/fake_hal.c tests/host/stubs/fake_usb.c tests/host/stubs/fake_adc_protocol_deps.c tests/host/stubs/fake_app_deps.c -o build/tests/host_tests.exe
build\tests\host_tests.exe
```

Result:

```text
[ RUN      ] adc_protocol
[     DONE ] adc_protocol
[ RUN      ] usb_stream
[     DONE ] usb_stream
[ RUN      ] app_smoke
[     DONE ] app_smoke
[  PASSED  ] host tests
```

Injection matrix:

| Injection | Expected behavior | Observed |
| --- | --- | --- |
| `adc_protocol_link_check()` config mismatch | Retry register read, record mismatch stats, return `ADC_PROTOCOL_ERR_CONFIG_MISMATCH` | Passed: retries = `APP_ADC_LINK_CHECK_RETRIES`, mismatch count = retries + 1 |
| USB CDC returns `USBD_BUSY` | Keep packet queued and retry later, increment `tx_busy` | Passed |
| USB sample queue overflow | Drop oldest sample, mark retained new frame with `SAMPLE_FLAG_USB_OVERFLOW`, increment `sample_overflow` | Passed |
| INIT `adc_protocol_reset()` returns `HAL_TIMEOUT` | Record `DIAG_FAULT_SPI_TIMEOUT`, set `SAMPLE_FLAG_SPI_TIMEOUT`, enter recovery instead of generic SPI error | Passed |
| COMM_CHECK DRDY timeout | Enter `APP_STATE_RECOVER`, run `reset + configure + link_check`, return to startup self-check | Passed |
| COMM_CHECK SPI read timeout | Enter `APP_STATE_RECOVER`, retry conversion, finish self-check when next sample succeeds | Passed |
| COMM_CHECK config mismatch | Enter `APP_STATE_RECOVER`, reconfigure ADC, return to startup self-check | Passed |
| Repeated config mismatch during recovery | Exhaust recovery attempts, record `DIAG_FAULT_RECOVERY_FAILED`, enter `APP_STATE_FAULT` with `SAMPLE_FLAG_RECOVERY_FAILED` | Passed |
| Fault frame after recovery failure | Preserve the root ADC protocol status in `reserved[7:0]` instead of `adc_protocol_stop()` status | Passed |
| FAULT reporting with USB busy/aux overflow | Count `DIAG_FAULT_USB_BUSY_OVERFLOW` without replacing the root `last_fault` | Passed |

Deterministic host injection recovery count: 5 recoverable scenarios passed out of 5 attempted recoverable scenarios, for 100% in this test set. This exceeds the V0.2 target of at least 80% recovery for injected transient faults.
