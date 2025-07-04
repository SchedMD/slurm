#!/usr/bin/env python3
############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import sys
import time


def main():
    if len(sys.argv) != 3:
        print("Usage: python allocate_memory.py <megabytes> <seconds>")
        sys.exit(1)

    try:
        megabytes = int(sys.argv[1])
        seconds = int(sys.argv[2])
    except ValueError:
        print("Both arguments must be integers.")
        sys.exit(1)

    print(f"Allocating {megabytes} MB for {seconds} seconds...")
    a = bytearray(megabytes * 1024 * 1024)

    # Touch every page to ensure it's resident (one byte every 4KB)
    for i in range(0, len(a), 4096):
        a[i] = 0

    time.sleep(seconds)
    print("Done.")


if __name__ == "__main__":
    main()
