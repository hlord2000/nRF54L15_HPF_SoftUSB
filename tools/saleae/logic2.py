"""Python-only connection to Saleae's MSO-capable headless preview server."""
import contextlib
import os
from pathlib import Path
import socket
import subprocess
import time

from saleae import automation

PORT = int(os.environ.get("LOGIC2_PORT", "10431"))


def _port_open(port):
    with socket.socket() as sock:
        sock.settimeout(0.2)
        return sock.connect_ex(("127.0.0.1", port)) == 0


@contextlib.contextmanager
def connect(timeout=10.0):
    """Attach to an existing server or own one bounded-lifetime child process.

    The GUI automation server cannot expose this MSO. Never silently fall back
    to simulated devices or start the GUI. A failed start is reported once.
    """
    proc = None
    log = None
    manager = None
    try:
        if not _port_open(PORT):
            server = Path(automation.__file__).resolve().parent.parent / "automation_server" / "logic_automation_server"
            if not server.exists():
                raise RuntimeError("Install the MSO preview wheel; headless server is missing")
            log_path = Path(os.environ.get("LOGIC2_LOG", "/tmp/softusb-saleae-server.log"))
            log = log_path.open("w")
            proc = subprocess.Popen([str(server), "--host", "127.0.0.1", "--port", str(PORT)],
                                    stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + timeout
            while not _port_open(PORT):
                if proc.poll() is not None:
                    raise RuntimeError(f"Headless server exited rc={proc.returncode}; see {log_path}")
                if time.monotonic() >= deadline:
                    raise TimeoutError(f"Headless startup timed out; see {log_path}")
                time.sleep(0.05)
        manager = automation.Manager.connect(port=PORT, connect_timeout_seconds=timeout)
        yield manager
    finally:
        if manager is not None:
            manager.close()
        # Signal only the exact child created above; no process-tree operations.
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
        if log is not None:
            log.close()


if __name__ == "__main__":
    with connect() as m:
        print(m.get_app_info())
        print(m.get_devices(include_simulation_devices=False))
