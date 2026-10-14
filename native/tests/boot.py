#!/usr/bin/env python3
"""Boot real machine-code isolation probes under BIOS and UEFI, using TCG."""
import argparse
import pathlib
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("iso", type=pathlib.Path)
parser.add_argument("--logs", type=pathlib.Path, default=pathlib.Path("/out"))
args = parser.parse_args()
args.logs.mkdir(parents=True, exist_ok=True)
cases = [("bios", "max", "512"), ("bios", "qemu64", "128"), ("uefi", "max", "512")]
for firmware, cpu, memory in cases:
    name = f"native-{firmware}-{cpu}"
    with tempfile.TemporaryDirectory() as tmp:
        command = ["qemu-system-x86_64", "-accel", "tcg", "-cpu", cpu,
                   "-m", memory, "-cdrom", str(args.iso), "-display", "none",
                   "-serial", "stdio", "-monitor", "none", "-no-reboot",
                   "-device", "isa-debug-exit,iobase=0xf4,iosize=4"]
        if firmware == "uefi":
            variables = pathlib.Path(tmp) / "vars.fd"
            shutil.copy("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
            command += ["-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
                        "-drive", f"if=pflash,format=raw,file={variables}"]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=75)
        output = result.stdout.decode(errors="replace")
        (args.logs / f"{name}.log").write_text(output)
        if result.returncode != 33 or "SELFTEST PASS 8/8" not in output or "PANIC:" in output:
            raise SystemExit(f"{name} failed ({result.returncode}):\n{output}")
        print(f"PASS {name}: 8 isolation/scheduling checks; sleep/wakeup and HLT idle", flush=True)
