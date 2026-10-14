#!/usr/bin/env python3
"""Exercise PS/2 input, the native console and ACPI power-off in a real VM."""
import argparse
import json
import pathlib
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("iso", type=pathlib.Path)
parser.add_argument("--out", type=pathlib.Path, default=pathlib.Path("/out"))
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
serial = args.out / "native-ui.log"
serial.unlink(missing_ok=True)

with tempfile.TemporaryDirectory() as tmp:
    qmp = pathlib.Path(tmp) / "qmp"
    vm = subprocess.Popen(["qemu-system-x86_64", "-accel", "tcg", "-cpu", "max",
                           "-m", "512", "-cdrom", str(args.iso), "-display", "none",
                           "-serial", f"file:{serial}", "-monitor", "none", "-no-reboot",
                           "-qmp", f"unix:{qmp},server=on,wait=off"])
    try:
        def wait_log(text, timeout=30):
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                if serial.exists() and text in serial.read_text(errors="replace"):
                    return
                if vm.poll() is not None:
                    raise AssertionError(f"VM exited before {text}")
                time.sleep(.05)
            raise AssertionError(f"Missing serial event: {text}")

        wait_log("SELFTEST PASS 8/8")
        connection = socket.socket(socket.AF_UNIX)
        connection.settimeout(5)
        connection.connect(str(qmp))
        stream = connection.makefile("rwb", buffering=0)
        stream.readline()

        def request(name, arguments=None):
            message = {"execute": name}
            if arguments is not None:
                message["arguments"] = arguments
            stream.write((json.dumps(message) + "\n").encode())
            while True:
                result = json.loads(stream.readline())
                if "error" in result:
                    raise AssertionError(result)
                if "return" in result:
                    return result["return"]

        def key(code):
            request("send-key", {"keys": [{"type": "qcode", "data": code}], "hold-time": 30})
            time.sleep(.08)

        def screenshot(name):
            time.sleep(.15)
            request("screendump", {"filename": str(args.out / f"native-{name}.ppm")})

        request("qmp_capabilities")
        screenshot("overview")
        key("f4")
        wait_log("UI: page 3")
        screenshot("security")
        # Move the PS/2 pointer from the center to the Devices sidebar entry.
        for dx, dy in [(-30, -5)] * 19 + [(-20, -5)]:
            request("input-send-event", {"events": [
                {"type": "rel", "data": {"axis": "x", "value": dx}},
                {"type": "rel", "data": {"axis": "y", "value": dy}}]})
            time.sleep(.03)
        for down in (True, False):
            request("input-send-event", {"events": [
                {"type": "btn", "data": {"button": "left", "down": down}}]})
            time.sleep(.08)
        wait_log("UI: page 2")
        key("f5")
        for command in ("uname", "mem", "test"):
            for character in command:
                key(character)
            key("ret")
            wait_log(f"CONSOLE: {command}")
        screenshot("console")
        key("f1")
        key("p")
        key("s")
        vm.wait(timeout=10)
        output = serial.read_text()
        assert vm.returncode == 0 and "POWER: shutdown requested" in output
        assert "PANIC:" not in output and "S5 unavailable" not in output
        print("PASS native UI: F-keys, mouse navigation, console, ACPI shutdown")
    finally:
        if vm.poll() is None:
            vm.terminate()
            vm.wait(timeout=5)
