# CMake generated Testfile for 
# Source directory: D:/code/MCU/STM32G431CBT_MCU/tests
# Build directory: D:/code/MCU/STM32G431CBT_MCU/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(host_tests "D:/code/MCU/STM32G431CBT_MCU/build/tests/Debug/host_tests.exe")
  set_tests_properties(host_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/code/MCU/STM32G431CBT_MCU/tests/CMakeLists.txt;44;add_test;D:/code/MCU/STM32G431CBT_MCU/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(host_tests "D:/code/MCU/STM32G431CBT_MCU/build/tests/Release/host_tests.exe")
  set_tests_properties(host_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/code/MCU/STM32G431CBT_MCU/tests/CMakeLists.txt;44;add_test;D:/code/MCU/STM32G431CBT_MCU/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(host_tests "D:/code/MCU/STM32G431CBT_MCU/build/tests/MinSizeRel/host_tests.exe")
  set_tests_properties(host_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/code/MCU/STM32G431CBT_MCU/tests/CMakeLists.txt;44;add_test;D:/code/MCU/STM32G431CBT_MCU/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(host_tests "D:/code/MCU/STM32G431CBT_MCU/build/tests/RelWithDebInfo/host_tests.exe")
  set_tests_properties(host_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/code/MCU/STM32G431CBT_MCU/tests/CMakeLists.txt;44;add_test;D:/code/MCU/STM32G431CBT_MCU/tests/CMakeLists.txt;0;")
else()
  add_test(host_tests NOT_AVAILABLE)
endif()
