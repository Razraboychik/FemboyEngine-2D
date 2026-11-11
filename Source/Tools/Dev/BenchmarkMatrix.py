#!/usr/bin/env python3
"""Run the high-resolution benchmark in alternating static/dynamic pairs.

The benchmark is sensitive to GPU clocks and thermal state. This wrapper runs
each mode repeatedly, alternates them, and reports the median CPU/GPU values.
"""
import argparse
import statistics
import subprocess
import sys


def parse_baseline(output):
    for line in output.splitlines():
        if line.startswith("baseline,"):
            fields = line.split(",")
            return float(fields[1]), float(fields[2]), float(fields[7])
    raise ValueError("benchmark output has no baseline row")


def run_once(binary, args, static, timeout):
    command = [binary, *args, "--only", "baseline"]
    if static:
        command.append("--static")
    try:
        completed = subprocess.run(command, check=False, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"benchmark timed out after {timeout}s") from error
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "benchmark failed")
    return parse_baseline(completed.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="build-dev/bin/tests/fby_optimization_ab.exe")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--width", type=int, default=15360)
    parser.add_argument("--height", type=int, default=8640)
    parser.add_argument("--sprites", type=int, default=15000)
    parser.add_argument("--lights", type=int, default=64)
    parser.add_argument("--frames", type=int, default=60)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()

    common = [
        "--sprites", str(args.sprites),
        "--lights", str(args.lights),
        "--frames", str(args.frames),
        "--width", str(args.width),
        "--height", str(args.height),
        "--scale", "1",
        "--fullview",
    ]
    # Warm the GPU once before the samples and alternate the order so a cold or
    # throttled clock does not systematically favour one mode.
    for _ in range(args.warmup):
        run_once(args.binary, common, False, args.timeout)

    static_samples = []
    dynamic_samples = []
    for sample in range(args.runs):
        if sample % 2 == 0:
            static_samples.append(run_once(args.binary, common, True, args.timeout))
            dynamic_samples.append(run_once(args.binary, common, False, args.timeout))
        else:
            dynamic_samples.append(run_once(args.binary, common, False, args.timeout))
            static_samples.append(run_once(args.binary, common, True, args.timeout))

    static_cpu = statistics.median(sample[0] for sample in static_samples)
    static_gpu = statistics.median(sample[1] for sample in static_samples)
    static_fps = statistics.median(sample[2] for sample in static_samples)
    dynamic_cpu = statistics.median(sample[0] for sample in dynamic_samples)
    dynamic_gpu = statistics.median(sample[1] for sample in dynamic_samples)
    dynamic_fps = statistics.median(sample[2] for sample in dynamic_samples)
    print(f"static:  cpu={static_cpu:.3f} ms gpu={static_gpu:.3f} ms fps={static_fps:.1f}")
    print(f"dynamic: cpu={dynamic_cpu:.3f} ms gpu={dynamic_gpu:.3f} ms fps={dynamic_fps:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
