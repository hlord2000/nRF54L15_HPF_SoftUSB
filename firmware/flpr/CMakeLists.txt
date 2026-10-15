cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(softusb_flpr)
# Actual NCS HPF assembly generation/check/install workflow.
zephyr_include_directories(../include)
hpf_assembly_install(app "${CMAKE_CURRENT_SOURCE_DIR}/src/hrt/hrt.c")
target_sources(app PRIVATE src/main.c)
