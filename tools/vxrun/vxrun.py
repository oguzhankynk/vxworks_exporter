#!/usr/bin/env python3
"""Run a VxWorks program in QEMU and exit with its exit status.

vxrun boots a VxWorks kernel under QEMU (by default the one that ships with
the VxWorks SDK in $WIND_HOME), hands the program to it on a throw-away FAT disk, runs it from the target
shell, streams its console output, and exits with the program's status. That
makes it usable as CMake's CROSSCOMPILING_EMULATOR, so `ctest` runs
cross-compiled tests as if they were native.

Two modes:

  RTP (default)   vxrun [options] <program.vxe> [args...]
                  Runs the program as a Real Time Process via vxrun_launcher,
                  which reports the exit code or the terminating signal.

  DKM             vxrun --dkm --entry <function> [options] <module.out>
                  Loads the kernel module with `ld`, calls <function>() from
                  the kernel shell and exits with 0 if it returned 0, 1
                  otherwise.

Exit status: the program's own status in RTP mode, 128+N when the program is
killed by signal N, 124 on timeout and 125 for vxrun's own errors (QEMU
missing, kernel not found, boot failure).

Only the Python standard library is used.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import select
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

EXIT_TIMEOUT = 124
EXIT_VXRUN_ERROR = 125

C_SHELL_PROMPT = b"-> "
CMD_SHELL_PROMPT = b"]# "
# Defaults match the QEMU kernel of the VxWorks SDK: a PC machine, an IDE disk
# (which that kernel mounts as /ata0a) and the SDK's documented boot line.
DEFAULT_BOOTLINE = "bootline:fs(0,0)host:vxWorks h=10.0.2.2 e=10.0.2.15 u=target pw=vxTarget o=gei0"
DEFAULT_TARGET_DISK = {"ide": "/ata0a", "nvme": "/nvme0a"}

EXIT_LINE = re.compile(rb"VXRUN-EXIT (code=(-?\d+)|signal=(\d+)|spawn-failed)")
VALUE_LINE = re.compile(rb"value = (-?\d+) = ")
NOISE_LINES = (
    re.compile(rb"^Launching process '.*' \.\.\.$"),
    re.compile(rb"^Process '.*' \(process Id = .*\) launched\.$"),
)


class VxrunError(Exception):
    """A failure of vxrun itself rather than of the program under test."""


class Timeout(Exception):
    """The target did not answer within the allowed time."""


def log(message: str) -> None:
    print(f"vxrun: {message}", file=sys.stderr, flush=True)


def find_kernel(wind_home: str | None, kernel: str | None) -> str:
    if kernel:
        if not os.path.isfile(kernel):
            raise VxrunError(f"kernel image not found: {kernel}")
        return kernel
    if not wind_home:
        raise VxrunError("pass --kernel, or set WIND_HOME to the VxWorks SDK directory")
    candidates = sorted(glob.glob(os.path.join(wind_home, "vxsdk", "bsps", "*", "vxWorks")))
    if not candidates:
        raise VxrunError(f"no kernel image under {wind_home}/vxsdk/bsps/*/vxWorks; pass --kernel")
    return candidates[0]


class Target:
    """A VxWorks guest in QEMU, driven over its serial console."""

    def __init__(self, cmd: list[str], verbose: bool):
        self.verbose = verbose
        self.buffer = b""
        if verbose:
            log("starting: " + " ".join(shlex.quote(c) for c in cmd))
        self.proc = subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )

    def read_until(self, pattern: re.Pattern[bytes] | bytes, deadline: float, start: int = 0):
        """Reads console output until pattern appears after offset start."""
        while True:
            haystack = self.buffer[start:]
            if isinstance(pattern, bytes):
                index = haystack.find(pattern)
                if index >= 0:
                    return start + index + len(pattern)
            else:
                match = pattern.search(haystack)
                if match:
                    return match
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise Timeout()
            ready, _, _ = select.select([self.proc.stdout], [], [], min(remaining, 0.2))
            if ready:
                chunk = os.read(self.proc.stdout.fileno(), 65536)
                if not chunk:
                    raise VxrunError("QEMU exited unexpectedly:\n" + self.text(self.buffer[-2000:]))
                self.buffer += chunk
                if self.verbose:
                    sys.stderr.write(self.text(chunk))

    def send(self, line: str) -> int:
        """Sends one shell line; returns the buffer offset just after its echo."""
        mark = len(self.buffer)
        self.proc.stdin.write(line.encode() + b"\n")
        self.proc.stdin.flush()
        return mark

    @staticmethod
    def text(data: bytes) -> str:
        return data.decode("utf-8", errors="replace").replace("\r", "")

    def close(self) -> None:
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait()


def qemu_command(qemu: str, kernel: str, stage_dir: str, args) -> list[str]:
    """The QEMU command line: the staged files appear as a FAT disk."""
    cmd = [qemu, "-snapshot", "-no-reboot"]  # -snapshot: never write back to the stage
    if args.machine:
        cmd += ["-machine", args.machine]
    if args.cpu:
        cmd += ["-cpu", args.cpu]
    cmd += ["-m", args.memory, "-smp", str(args.smp), "-kernel", kernel]
    if args.disk_if == "nvme":
        cmd += ["-drive", f"file=fat:{stage_dir},if=none,id=vxrun0,format=raw",
                "-device", "nvme,serial=vxrun0,drive=vxrun0"]
    else:
        cmd += ["-drive", f"file=fat:{stage_dir},format=raw,if=ide"]
    if args.network:
        nic = "user" + "".join(f",hostfwd={rule}" for rule in args.hostfwd)
        cmd += ["-nic", nic]
    else:
        cmd += ["-nic", "none"]
    cmd += ["-display", "none", "-serial", "stdio", "-monitor", "none"]
    if args.bootline:
        cmd += ["-append", args.bootline]
    cmd += args.qemu_arg
    return cmd


def stage_files(stage_dir: str, files: list[str], lib_dirs: list[str]) -> None:
    for path in files:
        shutil.copy2(path, stage_dir)
    for lib_dir in lib_dirs:
        for lib in glob.glob(os.path.join(lib_dir, "*.so*")):
            if os.path.isfile(lib):
                shutil.copy2(lib, stage_dir)


def quote_for_cmd_shell(arg: str) -> str:
    if arg and not re.search(r'[\s"\\]', arg):
        return arg
    return '"' + arg.replace("\\", "\\\\").replace('"', '\\"') + '"'


def program_output(raw: bytes) -> str:
    """Drops the shell's own launch messages from the program's output."""
    lines = []
    for line in raw.replace(b"\r", b"").split(b"\n"):
        if any(noise.match(line) for noise in NOISE_LINES):
            continue
        lines.append(line)
    return b"\n".join(lines).decode("utf-8", errors="replace")


def run_rtp(target: Target, args, deadline: float) -> int:
    target.read_until(C_SHELL_PROMPT, deadline)
    target.read_until(CMD_SHELL_PROMPT, deadline, target.send("cmd"))

    disk = args.target_disk
    program = os.path.basename(args.program)
    argv = [f"{disk}/{os.path.basename(args.launcher)}", f"{disk}/{program}"]
    argv += args.args
    line = "rtp exec -e LD_LIBRARY_PATH=" + disk + " " + " ".join(
        quote_for_cmd_shell(a) for a in argv
    )
    start = target.send(line)
    echo_end = target.read_until(b"\n", deadline, start)
    match = target.read_until(EXIT_LINE, deadline, echo_end)

    output = program_output(target.buffer[echo_end : echo_end + match.start()])
    if output.strip():
        sys.stdout.write(output if output.endswith("\n") else output + "\n")
        sys.stdout.flush()

    if match.group(2) is not None:
        return int(match.group(2)) & 0xFF
    if match.group(3) is not None:
        signo = int(match.group(3))
        try:
            name = signal.Signals(signo).name
        except ValueError:
            name = f"signal {signo}"
        log(f"{program} was killed by {name}")
        return 128 + signo
    log(f"could not start {program} on the target")
    return EXIT_VXRUN_ERROR


def run_dkm(target: Target, args, deadline: float) -> int:
    target.read_until(C_SHELL_PROMPT, deadline)
    module = f"{args.target_disk}/{os.path.basename(args.program)}"

    start = target.send(f"ld < {module}")
    end = target.read_until(C_SHELL_PROMPT, deadline, start + 1)
    load_output = target.text(target.buffer[start:end])
    if "error" in load_output.lower() or "undefined" in load_output.lower():
        sys.stdout.write(load_output)
        log(f"loading {module} failed")
        return 1

    start = target.send(args.entry)
    echo_end = target.read_until(b"\n", deadline, start)
    match = target.read_until(VALUE_LINE, deadline, echo_end)
    output = target.text(target.buffer[echo_end : echo_end + match.start()])
    if output.strip():
        sys.stdout.write(output if output.endswith("\n") else output + "\n")
        sys.stdout.flush()
    value = int(match.group(1))
    if value != 0:
        log(f"{args.entry}() returned {value}")
    return 0 if value == 0 else 1


def parse_args(argv: list[str]):
    parser = argparse.ArgumentParser(
        prog="vxrun", description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wind-home", default=os.environ.get("WIND_HOME"),
                        help="VxWorks SDK directory, to find its QEMU kernel (default: $WIND_HOME)")
    parser.add_argument("--kernel", help="kernel image to boot (default: the SDK's QEMU kernel)")
    parser.add_argument("--qemu", default=os.environ.get("VXRUN_QEMU", "qemu-system-x86_64"),
                        help="QEMU binary (default: qemu-system-x86_64)")
    parser.add_argument("--memory", default="2048M", help="guest memory (default: 2048M)")
    parser.add_argument("--smp", type=int, default=1, help="guest CPUs (default: 1)")
    parser.add_argument("--machine", help="QEMU machine type, e.g. q35 (default: QEMU's)")
    parser.add_argument("--cpu", help="QEMU CPU model, e.g. Broadwell-noTSX (default: QEMU's)")
    parser.add_argument("--disk-if", choices=["ide", "nvme"], default="ide",
                        help="how the FAT disk with the program is attached (default: ide)")
    parser.add_argument("--target-disk",
                        help="that disk's device name on the target (default: /ata0a for ide, /nvme0a for nvme)")
    parser.add_argument("--bootline", default=DEFAULT_BOOTLINE,
                        help="kernel boot line passed with -append (default: the SDK's)")
    parser.add_argument("--no-bootline", dest="bootline", action="store_const", const="",
                        help="pass no -append; the kernel uses its built-in boot line")
    parser.add_argument("--no-network", dest="network", action="store_false",
                        help="start the guest without a network card")
    parser.add_argument("--hostfwd", action="append", default=[],
                        help="forward a host port into the guest, e.g. tcp:127.0.0.1:19100-:9100 "
                             "(repeatable)")
    parser.add_argument("--qemu-arg", action="append", default=[],
                        help="extra QEMU argument (repeatable)")
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("VXRUN_TIMEOUT", 300)),
                        help="seconds for boot plus run (default: 300)")
    parser.add_argument("--launcher", help="vxrun_launcher.vxe (required in RTP mode)")
    parser.add_argument("--lib-dir", action="append", default=[],
                        help="directory whose *.so files are staged next to the program")
    parser.add_argument("--file", action="append", default=[],
                        help="extra file to stage on the target disk")
    parser.add_argument("--dkm", action="store_true", help="load a kernel module instead of an RTP")
    parser.add_argument("--entry", help="function to call after loading (DKM mode)")
    parser.add_argument("-v", "--verbose", action="store_true", help="echo the whole console")
    parser.add_argument("program", help="program (.vxe) or kernel module (.out)")
    parser.add_argument("args", nargs=argparse.REMAINDER, help="arguments for the program")
    args = parser.parse_args(argv)
    if args.args and args.args[0] == "--":
        args.args = args.args[1:]
    if not args.target_disk:
        args.target_disk = DEFAULT_TARGET_DISK[args.disk_if]
    if args.dkm and not args.entry:
        parser.error("--dkm needs --entry")
    if not args.dkm and not args.launcher:
        parser.error("RTP mode needs --launcher")
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        kernel = find_kernel(args.wind_home, args.kernel)
        qemu = shutil.which(args.qemu)
        if not qemu:
            raise VxrunError(f"{args.qemu} not found; install QEMU (e.g. apt install qemu-system-x86)")
        for path in [args.program] + ([] if args.dkm else [args.launcher]) + args.file:
            if not os.path.isfile(path):
                raise VxrunError(f"file not found: {path}")
    except VxrunError as error:
        log(str(error))
        return EXIT_VXRUN_ERROR

    with tempfile.TemporaryDirectory(prefix="vxrun-") as stage_dir:
        files = [args.program] + args.file + ([] if args.dkm else [args.launcher])
        stage_files(stage_dir, files, args.lib_dir)
        target = Target(qemu_command(qemu, kernel, stage_dir, args), args.verbose)
        deadline = time.monotonic() + args.timeout
        try:
            if args.dkm:
                return run_dkm(target, args, deadline)
            return run_rtp(target, args, deadline)
        except Timeout:
            log(f"timed out after {args.timeout:.0f}s; last console output:")
            sys.stderr.write(target.text(target.buffer[-2000:]) + "\n")
            return EXIT_TIMEOUT
        except VxrunError as error:
            log(str(error))
            return EXIT_VXRUN_ERROR
        finally:
            target.close()


def exit_on_sigterm() -> None:
    """Turns SIGTERM (ctest timeouts, kill) into SystemExit, so `finally`
    blocks run and QEMU is not left behind."""
    def handler(signum, _frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, handler)


if __name__ == "__main__":
    exit_on_sigterm()
    sys.exit(main(sys.argv[1:]))
