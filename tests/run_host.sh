#!/bin/sh
# Invoke ONLY via Backplane work_run, lock bend, inside the requested resource
# scope. This script accesses host files only; it never flashes or probes a DUT.
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/.build
CC=${CC:-cc}
case "${1:-}" in
    ""|--skip-native) ;;
    *) echo "usage: $0 [--skip-native]" >&2; exit 2 ;;
esac
WARN='-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Werror'
# Normal portable C99 build used by ctypes and the native executable.
"$CC" -std=c99 $WARN -O2 -fPIC -shared -Icommon common/softusb.c common/softusb_device.c -o tests/.build/libsoftusb.so
if [ "${1:-}" != --skip-native ]; then
    "$CC" -std=c99 $WARN -O2 -Icommon common/softusb.c common/softusb_device.c tests/test_softusb.c -o tests/.build/test_softusb
    tests/.build/test_softusb
fi
# ASan and UBSan independently check native buffers and state transitions.
"$CC" -std=c99 $WARN -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Icommon common/softusb.c common/softusb_device.c tests/test_softusb.c -o tests/.build/test_softusb_sanitize
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 tests/.build/test_softusb_sanitize
# Use project uv environment if available, with no third-party dependencies.
if command -v uv >/dev/null 2>&1; then
    uv run --no-project --no-sync python tests/test_reference_usb.py --library tests/.build/libsoftusb.so
else
    python3 tests/test_reference_usb.py --library tests/.build/libsoftusb.so
fi
