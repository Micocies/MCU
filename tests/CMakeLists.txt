cmake_minimum_required(VERSION 3.16)

project(mcu_host_tests C)

enable_testing()

add_executable(host_tests
  ../Core/Src/adc_protocol.c
  ../Core/Src/usb_stream.c
  ../Core/Src/app.c
  host/main.c
  host/test_adc_protocol.c
  host/test_usb_stream.c
  host/test_app_smoke.c
  host/stubs/fake_hal.c
  host/stubs/fake_usb.c
  host/stubs/fake_adc_protocol_deps.c
  host/stubs/fake_app_deps.c
)

target_compile_definitions(host_tests PRIVATE UNIT_TEST)

target_include_directories(host_tests PRIVATE
  host/include
  ../Core/Inc
)

if(MSVC)
  target_compile_options(host_tests PRIVATE /W4)
else()
  target_compile_options(host_tests PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME host_tests COMMAND host_tests)
