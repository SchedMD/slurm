#!/usr/bin/env python3
############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
"""
Generate a controlled memory-pressure curve: peak, valley, peak, ...

Each iteration prints `[prefix] <VmHWM> <VmRSS>` (kB) to stdout.
Progress markers "Allocating <MIB_PEAK> MiB..." and "Done." go to stderr.
"""

import argparse
import mmap
import sys
import time


def read_proc_status(key):
    key = key.lower()
    with open("/proc/self/status", "r") as status:
        for line in status:
            name, _, value = line.partition(":")
            if name.lower() == key:
                return int(value.split()[0])
    raise RuntimeError(f"missing field in /proc/self/status: {key}")


def allocate_and_touch(mib):
    """Allocate `mib` MiB of anonymous memory and touch every page so it
    becomes resident. Returns the mmap, or None when mib <= 0.
    """
    if mib <= 0:
        return None
    nbytes = mib * 1024 * 1024
    mapping = mmap.mmap(-1, nbytes)
    for offset in range(0, nbytes, mmap.PAGESIZE):
        mapping[offset] = 1
    return mapping


def release(mapping):
    if mapping is not None:
        mapping.close()


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "mem_peak",
        type=int,
        metavar="MIB_PEAK",
        help="MiB to allocate during the peak phase",
    )
    parser.add_argument(
        "time_mem_peak",
        type=float,
        nargs="?",
        default=5.0,
        metavar="TIME_MEM_PEAK",
        help="seconds to hold the peak (default: 5.0)",
    )
    parser.add_argument(
        "--time-mem-valley",
        type=float,
        default=0.0,
        help="seconds to hold the valley after release (default: 0 = no valley phase)",
    )
    parser.add_argument(
        "--mem-valley",
        type=int,
        default=0,
        help="MiB to hold during the valley phase (default: 0 = full release)",
    )
    parser.add_argument(
        "--iterations",
        type=int,
        default=1,
        help="number of peak-valley cycles to run (default: 1)",
    )
    parser.add_argument(
        "--prefix",
        default="",
        help="string emitted before the numbers on each stdout report line "
        "(default: empty = numbers only)",
    )
    args = parser.parse_args()

    if args.mem_peak <= 0:
        parser.error("MIB_PEAK must be greater than 0")
    if args.iterations < 1:
        parser.error("--iterations must be at least 1")
    if args.time_mem_peak < 0:
        parser.error("TIME_MEM_PEAK cannot be negative")
    if args.time_mem_valley < 0:
        parser.error("--time-mem-valley cannot be negative")
    if args.mem_valley < 0:
        parser.error("--mem-valley cannot be negative")

    print(f"Allocating {args.mem_peak} MiB...", file=sys.stderr, flush=True)

    for _ in range(args.iterations):
        peak_mapping = allocate_and_touch(args.mem_peak)
        if args.time_mem_peak > 0:
            time.sleep(args.time_mem_peak)
        release(peak_mapping)

        if args.time_mem_valley > 0:
            valley_mapping = allocate_and_touch(args.mem_valley)
            time.sleep(args.time_mem_valley)
            release(valley_mapping)

        # VmHWM is the monotonic high-water peak (unchanged across iterations
        # for a fixed mem_peak); VmRSS is the current resident set after release.
        fields = []
        if args.prefix:
            fields.append(args.prefix)
        fields.append(read_proc_status("VmHWM"))
        fields.append(read_proc_status("VmRSS"))
        print(*fields, flush=True)

    print("Done.", file=sys.stderr, flush=True)


if __name__ == "__main__":
    main()
