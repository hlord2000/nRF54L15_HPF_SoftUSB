#!/usr/bin/env bash
# Invoke ONLY through Backplane work_run with lock:"bend" (machine-wide).
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${1:-} != --scoped-worker ]]; then
    exec systemd-run --user --scope -p MemoryMax=8G -p CPUQuota=400% \
        "$0" --scoped-worker "$@"
fi
shift
sdk_root=${NCS_ROOT:-/home/h/ncs/v3.4.1}
toolchain_root=${NCS_TOOLCHAIN:-/home/h/ncs/toolchains/8285d8ad56}
# Nordic's toolchain manifest provides the exact bundled Python and SDK paths.
eval "$(python3 - "$toolchain_root" <<'PY'
import json, os, shlex, sys
root = sys.argv[1]
for item in json.load(open(root + '/environment.json'))['env_vars']:
    key = item['key']
    if item['type'] == 'relative_paths':
        value = ':'.join(root + '/' + p for p in item['values'])
        if item['existing_value_treatment'] == 'prepend_to' and os.environ.get(key):
            value += ':' + os.environ[key]
    else:
        value = item['value']
    print('export ' + key + '=' + shlex.quote(value))
PY
)"
export ZEPHYR_BASE="$sdk_root/zephyr"
export CMAKE_BUILD_PARALLEL_LEVEL=4
cd "$sdk_root"
exec west build --sysbuild -b nrf54l15dk/nrf54l15/cpuapp \
    -d "$project_root/firmware/build" "$project_root/firmware" \
    --build-opt=-j4 "$@"
