# Custom HPF personality: NCS only supplies GPIO and MSPI personalities.
ExternalZephyrProject_Add(
  APPLICATION softusb_flpr
  SOURCE_DIR ${APP_DIR}/flpr
  BOARD nrf54l15dk/nrf54l15/cpuflpr
)
add_dependencies(${DEFAULT_IMAGE} softusb_flpr)
