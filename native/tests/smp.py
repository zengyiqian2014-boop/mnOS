#!/usr/bin/env python3
"""Start real APs, then require isolation/scheduling checks on every online CPU."""
import argparse
import pathlib
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("iso", type=pathlib.Path)
parser.add_argument("--logs", type=pathlib.Path, default=pathlib.Path("/out"))
args = parser.parse_args()
cases = [("bios", "max,x2apic=off", 4, "xAPIC"),
         ("bios", "max", 8, "x2APIC"),
         ("uefi", "max", 4, "x2APIC"),
         ("bios", "max", 32, "x2APIC"),
         ("bios", "max", 2, "x2APIC")]
for firmware, model, cores, mode in cases:
    name = f"native-smp-{firmware}-{mode}-{cores}"
    with tempfile.TemporaryDirectory() as tmp:
        command = ["qemu-system-x86_64", "-accel", "tcg", "-cpu", model,
                   "-smp", str(cores), "-m", "512", "-cdrom", str(args.iso),
                   "-display", "none", "-serial", "stdio", "-monitor", "none", "-no-reboot",
                   "-device", "isa-debug-exit,iobase=0xf4,iosize=4"]
        if cores == 2:
            # ACPI x2APIC records and sparse IDs must not alias an 8-bit CPU index.
            command[command.index("-smp") + 1] = "1,maxcpus=512,sockets=2,cores=256,threads=1"
            command += ["-machine", "q35", "-device",
                        "max-x86_64-cpu,apic-id=256,socket-id=1,core-id=0,thread-id=0"]
        if firmware == "uefi":
            variables = pathlib.Path(tmp) / "vars.fd"
            shutil.copy("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
            command += ["-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
                        "-drive", f"if=pflash,format=raw,file={variables}"]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        output = result.stdout.decode(errors="replace")
        (args.logs / f"{name}.log").write_text(output)
        assert result.returncode == 33 and "PANIC:" not in output, output
        assert f"SMP: discovered={cores} mode={mode}" in output, output
        assert f"SMP: online={cores}\n" in output, output
        assert f"SMP PASS {cores}/{cores} processors" in output, output
        if cores == 2:
            assert "SMP: online APIC ID 256\n" in output, output
        print(f"PASS {name}: every CPU completed ring-3 isolation, GS reset, preemption and HLT", flush=True)
