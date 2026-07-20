############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import itertools
import sys
import time

BILLION = 1_000_000_000


def main():
    """Print time every second and report detected process suspensions.

    Takes an optional positive-integer argument for the number of iterations;
    with no argument (or a non-positive one) it loops forever until killed.

    Additionally, this method attempts to determine if it has been suspended
    and, upon determination, prints 'JobSuspended' once the process is resumed.
    Because programs can't catch suspension signals like 'SIGSTOP', this method
    instead determines it was suspended if it hasn't updated the variable 'last'
    in the last 3 seconds. Note that all time measurements are performed in
    nanoseconds.
    """

    if len(sys.argv) > 1 and int(sys.argv[1]) > 0:
        counter = range(int(sys.argv[1]), 0, -1)
    else:
        # Infinite counter
        counter = itertools.count(1)

    last = time.monotonic_ns()
    for i in counter:
        print(f"{i:02d} {last}")
        sys.stdout.flush()
        time.sleep(1)
        now = time.monotonic_ns()
        if now > (last + 3 * BILLION):
            print("JobSuspended")
        last = now

    print("AllDone")


if __name__ == "__main__":
    main()
