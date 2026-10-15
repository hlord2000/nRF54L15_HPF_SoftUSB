The same C assertion bodies in `test_softusb.c` run as a plain native executable,
under AddressSanitizer/UndefinedBehaviorSanitizer, and through Zephyr ztest.
`test_reference_usb.py` loads the native shared library and compares its packet
bytes, parsed fields, physical line states, and malformed-input errors with the
separate Python model in `tools/reference_usb.py`.

All heavy runs must be submitted with Backplane `work_run`, lock `bend`, using:

```
systemd-run --user --scope -p MemoryMax=8G -p CPUQuota=400% /bin/sh tests/run_host.sh
```

Build products stay in `tests/.build/` and must not be committed. The Python
reference has no third-party dependencies; the host runner prefers `uv`.

For Zephyr/NCS v3.4.1, root can run Twister using the configured SDK environment,
through the same lock/resource scope (with this project as the working directory):

```
python /home/h/ncs/v3.4.1/zephyr/scripts/twister \
  -T tests/zephyr -p native_sim/native/64 -j 4 \
  --outdir tests/.build/twister
```

The Zephyr wrapper allocates a 64 KiB test stack because the shared suite exercises
maximum FS buffers as well as LS packets. Its two ztest cases call the existing
C suite; assertions are not copied. It does not use a physical DUT.

These are ideal packet/bit-cell and device transaction simulations, not USB
enumeration, electrical, timing, or interoperability tests. A successful native
run does not imply that native_sim was run. Report each actual runner separately.
