#!/usr/bin/env python3
"""Boot VxWorks in QEMU with the exporter running, for the demo.

Loads vxworks_exporter.out (and, unless --no-load, the synthetic workload
vxexp_demo_load.out), starts the exporter on the target's port 9100 and
forwards it to the host. Runs until Ctrl-C or --duration seconds.

  demo/run_target.py --build build/vx-dkm            # http://127.0.0.1:9100/metrics
  demo/run_target.py --build build/vx-dkm --listen 0.0.0.0   # reachable from Docker

Needs WIND_HOME (the VxWorks SDK) or --kernel, like tools/vxrun/vxrun.py.
Other vxrun QEMU options (--machine, --cpu, --smp, ...) are passed through.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import re
import signal
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
VXRUN = os.path.join(HERE, "..", "tools", "vxrun", "vxrun.py")


def load_vxrun():
    spec = importlib.util.spec_from_file_location("vxrun", VXRUN)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--build", required=True, help="a vx-dkm build directory")
    parser.add_argument("--listen", default="127.0.0.1", help="host address to forward from")
    parser.add_argument("--port", type=int, default=9100, help="host port (default 9100)")
    parser.add_argument("--duration", type=float, default=0, help="seconds to run; 0 = until Ctrl-C")
    parser.add_argument("--no-load", action="store_true", help="do not start the demo workload")
    parser.add_argument("--quiet", action="store_true", help="do not echo the VxWorks console")
    own, qemu_options = parser.parse_known_args(argv)

    exporter = os.path.join(own.build, "src", "vxworks_exporter.out")
    load = os.path.join(own.build, "src", "vxexp_demo_load.out")
    files = [exporter] + ([] if own.no_load else [load])
    for path in files:
        if not os.path.isfile(path):
            print(f"run_target: {path} not found; build the vx-dkm preset first", file=sys.stderr)
            return 2

    vxrun = load_vxrun()
    args = vxrun.parse_args(qemu_options + [
        "--dkm", "--entry", "vxExporterStart",
        "--hostfwd", f"tcp:{own.listen}:{own.port}-:9100", exporter])
    kernel = vxrun.find_kernel(args.wind_home, args.kernel)

    with tempfile.TemporaryDirectory(prefix="vxexp-demo-") as stage:
        vxrun.stage_files(stage, files, [])
        target = vxrun.Target(vxrun.qemu_command(args.qemu, kernel, stage, args), not own.quiet)
        try:
            deadline = time.monotonic() + 120
            target.read_until(vxrun.C_SHELL_PROMPT, deadline)
            commands = [f"ld < {args.target_disk}/{os.path.basename(f)}" for f in files]
            commands.append("vxExporterStart 0, 9100")
            if not own.no_load:
                commands.append("vxExporterDemoLoad")
            for command in commands:
                start = target.send(command)
                # Wait for the command's result and the next prompt before
                # typing again: the shell drops input that arrives mid-output.
                result = target.read_until(re.compile(rb"value = [^\n]*\n"), deadline, start)
                target.read_until(vxrun.C_SHELL_PROMPT, deadline, start + result.end())
            print(f"\nrun_target: exporter at http://{own.listen}:{own.port}/metrics "
                  "(Ctrl-C to stop)", file=sys.stderr, flush=True)
            end = time.monotonic() + own.duration if own.duration > 0 else float("inf")
            while time.monotonic() < end:
                try:
                    target.read_until(b"\x00never\x00", min(end, time.monotonic() + 5))
                except vxrun.Timeout:
                    pass
        except KeyboardInterrupt:
            pass
        finally:
            target.close()
    return 0


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda signum, _frame: sys.exit(128 + signum))
    sys.exit(main(sys.argv[1:]))
