#!/usr/bin/env python3
"""End-to-end test: scrape the real exporter module from the host.

Boots VxWorks in QEMU (through tools/vxrun/vxrun.py), loads
vxworks_exporter.out, starts it, forwards a free host port to the target's
port 9100, and scrapes /metrics twice from the host. Checks:

  * both responses are HTTP 200 with valid exposition text;
  * the expected metric families are present;
  * counters never decrease between scrapes;
  * the CPU time all tasks accumulated between the scrapes matches the target
    time that passed (the accounting neither loses nor invents time);
  * the network interface counters see the scrape traffic;
  * vxExporterStop() succeeds.

Usage: scrape_test.py --vxrun <vxrun.py> --module <vxworks_exporter.out>
                      [--kernel K] [other vxrun QEMU options]
Exit status 0 on success, 1 on a failed check, 125 on setup errors.
"""

from __future__ import annotations

import argparse
import importlib.util
import math
import re
import signal
import socket
import sys
import tempfile
import time
import urllib.request

SAMPLE = re.compile(r'^([a-zA-Z_:][a-zA-Z0-9_:]*)(\{(.*)\})? (\S+)$')
LABEL = re.compile(r'([a-zA-Z_][a-zA-Z0-9_]*)="((?:[^"\\]|\\.)*)"')

EXPECTED_FAMILIES = [
    "vxworks_build_info", "vxworks_uptime_seconds", "vxworks_cpus", "vxworks_tasks",
    "vxworks_cpu_idle_seconds_total", "vxworks_task_cpu_seconds_total", "vxworks_task_priority",
    "vxworks_task_state", "vxworks_task_stack_size_bytes", "vxworks_task_stack_used_max_bytes",
    "vxworks_kernel_heap_free_bytes", "vxworks_rtps", "vxworks_network_receive_bytes_total",
    "vxexporter_scrapes_total", "vxexporter_scrape_duration_seconds",
]

failures = 0


def check(ok: bool, what: str) -> None:
    global failures
    print(f"[{'  OK  ' if ok else ' FAIL '}] {what}", flush=True)
    if not ok:
        failures += 1


def parse(text: str):
    """Returns ({family: type}, {(name, frozenset(labels)): value})."""
    types, samples = {}, {}
    for line in text.splitlines():
        if line.startswith("# TYPE "):
            _, _, name, kind = line.split(" ", 3)
            types[name] = kind
        elif line and not line.startswith("#"):
            m = SAMPLE.match(line)
            if not m:
                raise ValueError(f"bad sample line: {line!r}")
            labels = frozenset(LABEL.findall(m.group(3) or ""))
            samples[(m.group(1), labels)] = float(m.group(4))
    return types, samples


def values(samples, name):
    return {labels: v for (n, labels), v in samples.items() if n == name}


def scrape(port: int):
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/metrics", timeout=30) as response:
        return response.status, response.headers.get("Content-Type", ""), response.read().decode()


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def load_vxrun(path: str):
    spec = importlib.util.spec_from_file_location("vxrun", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vxrun", required=True)
    parser.add_argument("--module", required=True)
    own, qemu_options = parser.parse_known_args(argv)

    vxrun = load_vxrun(own.vxrun)
    port = free_port()
    args = vxrun.parse_args(qemu_options + [
        "--dkm", "--entry", "vxExporterStart", "--hostfwd", f"tcp:127.0.0.1:{port}-:9100",
        own.module])
    try:
        kernel = vxrun.find_kernel(args.wind_home, args.kernel)
    except vxrun.VxrunError as error:
        print(f"scrape_test: {error}", file=sys.stderr)
        return 125

    with tempfile.TemporaryDirectory(prefix="vxexp-e2e-") as stage:
        vxrun.stage_files(stage, [own.module], [])
        target = vxrun.Target(vxrun.qemu_command(args.qemu, kernel, stage, args), False)
        deadline = time.monotonic() + 240
        try:
            target.read_until(vxrun.C_SHELL_PROMPT, deadline)
            module = f"{args.target_disk}/{own.module.rsplit('/', 1)[-1]}"
            start = target.send(f"ld < {module}")
            end = target.read_until(vxrun.C_SHELL_PROMPT, deadline, start + 1)
            load = target.text(target.buffer[start:end])
            check("undefined" not in load.lower() and "error" not in load.lower(),
                  "module loads without undefined symbols")

            start = target.send("vxExporterStart 0, 9100")
            target.read_until(re.compile(rb"value = 0 = "), deadline, start)
            check(True, "vxExporterStart returns 0")

            status, ctype, first = scrape(port)
            time.sleep(3)
            status2, _, second = scrape(port)
            check(status == 200 and status2 == 200, "GET /metrics -> 200 twice")
            check(ctype.startswith("text/plain; version=0.0.4"), f"content type ({ctype})")

            types1, s1 = parse(first)
            types2, s2 = parse(second)
            for family in EXPECTED_FAMILIES:
                check(family in types2, f"family {family}")

            decreased = [
                (name, dict(labels)) for (name, labels), v in s1.items()
                if types1.get(name) == "counter" and (name, labels) in s2 and s2[(name, labels)] < v
            ]
            check(not decreased, f"no counter decreased {decreased[:3]}")

            up1 = values(s1, "vxworks_uptime_seconds")[frozenset()]
            up2 = values(s2, "vxworks_uptime_seconds")[frozenset()]
            elapsed = up2 - up1
            cpus = values(s2, "vxworks_cpus")[frozenset()]
            cpu1 = values(s1, "vxworks_task_cpu_seconds_total")
            cpu2 = values(s2, "vxworks_task_cpu_seconds_total")
            used = sum(v - cpu1.get(labels, 0.0) for labels, v in cpu2.items())
            ratio = used / (elapsed * cpus) if elapsed > 0 else math.nan
            check(0.8 <= ratio <= 1.2,
                  f"CPU time between scrapes matches elapsed time (ratio {ratio:.2f})")

            exporter = [v for labels, v in cpu2.items() if ("task", "tVxExporter") in labels]
            check(bool(exporter) and exporter[0] > 0, "tVxExporter has used CPU time")

            rx1 = values(s1, "vxworks_network_receive_bytes_total")
            rx2 = values(s2, "vxworks_network_receive_bytes_total")
            check(any(v > rx1.get(labels, 0.0) for labels, v in rx2.items()),
                  "an interface's received bytes grew with the scrape traffic "
                  f"({sorted((dict(k).get('device'), v) for k, v in rx2.items())})")

            scrapes = values(s2, "vxexporter_scrapes_total")[frozenset()]
            check(scrapes >= 2, f"vxexporter_scrapes_total counts the scrapes ({scrapes:.0f})")

            start = target.send("vxExporterStop")
            target.read_until(re.compile(rb"value = 0 = "), deadline, start)
            check(True, "vxExporterStop returns 0")
        except vxrun.Timeout:
            check(False, "target answered in time; last console output:\n"
                  + target.text(target.buffer[-2000:]))
        except (OSError, ValueError, KeyError) as error:
            check(False, f"scrape failed: {error!r}\n" + target.text(target.buffer[-2000:]))
        finally:
            target.close()

    print(f"{failures} check(s) failed")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda signum, _frame: sys.exit(128 + signum))
    sys.exit(main(sys.argv[1:]))
