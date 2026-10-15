#!/usr/bin/env python3
"""Exercise PS/2 input, the native console and ACPI power-off in a real VM."""
import argparse
import json
import pathlib
import shutil
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("iso", type=pathlib.Path)
parser.add_argument("--out", type=pathlib.Path, default=pathlib.Path("/out"))
parser.add_argument("--gpu", choices=("std", "virtio"), default="std")
parser.add_argument("--uefi", action="store_true")
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
label = "ui" if args.gpu == "std" and not args.uefi else f"ui-{args.gpu}-{'uefi' if args.uefi else 'bios'}"
serial = args.out / f"native-{label}.log"
serial.unlink(missing_ok=True)

with tempfile.TemporaryDirectory() as tmp:
    qmp = pathlib.Path(tmp) / "qmp"
    options = ["qemu-system-x86_64", "-accel", "tcg", "-cpu", "max",
                           "-m", "512", "-cdrom", str(args.iso), "-display", "none",
                           "-serial", f"file:{serial}", "-monitor", "none", "-no-reboot",
                           "-qmp", f"unix:{qmp},server=on,wait=off", "-vga", args.gpu]
    if args.uefi:
        variables = pathlib.Path(tmp) / "vars.fd"
        shutil.copy('/usr/share/OVMF/OVMF_VARS_4M.fd', variables)
        options += ['-drive', 'if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd',
                    '-drive', f'if=pflash,format=raw,file={variables}']
    vm = subprocess.Popen(options)
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
            path = args.out / f"native-{label}-{name}.ppm"
            request("screendump", {"filename": str(path)})
            return path

        request("qmp_capabilities")
        screenshot("login")
        def type_line(value):
            for character in value:
                key(character)
            key("ret")
        type_line("demo")
        type_line("ordinarytest491")
        type_line("ordinarytest491")
        wait_log("Set a separate password for root", timeout=180)
        type_line("rootadmintest785")
        type_line("rootadmintest785")
        wait_log("Accounts created. Signed in as the ordinary user.", timeout=180)
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
        for command in ("uname", "mem", "test", "help", "help -cn", "kernel help", "kernel help -cn"):
            for character in command:
                key("spc" if character == " " else "minus" if character == "-" else character)
            key("ret")
            wait_log(f"CONSOLE: {command}")
        wait_log("OUT: mnOS command guide (English)")
        wait_log("OUT: mnOS 中文帮助")
        screenshot("console")
        key("f7")
        wait_log("UI: page 6")
        key("r")
        wait_log("DISPLAY: 1600x900 / DISPI")
        key("w")
        wait_log("SETTINGS: wallpaper=1")
        settings = screenshot("settings")
        assert settings.read_bytes().splitlines()[1] == b"1600 900"
        # VirtIO VGA's legacy framebuffer has 8 MiB, so skip 2560x1440 (14 MiB).
        modes = [(1920, 1080)] + ([(2560, 1440)] if args.gpu == "std" else [])
        for mode in modes + [(1024, 768), (1280, 720), (1600, 900)]:
            key("r")
            wait_log(f"DISPLAY: {mode[0]}x{mode[1]} / DISPI")
            capture = screenshot("modes")
            assert capture.read_bytes().splitlines()[1] == f"{mode[0]} {mode[1]}".encode()
        key("f1")
        key("p")
        key("s")
        vm.wait(timeout=10)
        output = serial.read_text()
        assert vm.returncode == 0 and "POWER: shutdown requested" in output
        assert "PANIC:" not in output and "S5 unavailable" not in output
        assert 'ordinarytest491' not in output and 'rootadmintest785' not in output
        print("PASS native UI: account enrollment, hidden passwords, ordinary-user F1-F7, mouse, bilingual help, resolution, wallpaper, ACPI shutdown")
    finally:
        if vm.poll() is None:
            vm.terminate()
            vm.wait(timeout=5)
