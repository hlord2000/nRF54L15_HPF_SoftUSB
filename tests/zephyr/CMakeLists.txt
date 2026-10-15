cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(softusb_host_tests LANGUAGES C)

target_include_directories(app PRIVATE ../../common)
target_compile_definitions(app PRIVATE SOFTUSB_ZTEST=1)
target_sources(app PRIVATE
  src/main.c
  ../test_softusb.c
  ../../common/softusb.c
  ../../common/softusb_device.c
)
