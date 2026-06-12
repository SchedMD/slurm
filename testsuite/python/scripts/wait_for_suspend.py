############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import time
import sys

BILLION = 1_000_000_000


def main():
    """Print time every second and report detected process suspensions.

    Prints every second some information for 'start' number of seconds. Takes in
    an optional integer terminal argument for the value of 'start' -- defaults
    to 30 if no such argument is given.

    Additionally, this method attempts to determine if it has been suspended
    and, upon determination, prints 'JobSuspended' once the process is resumed.
    Because programs can't catch suspension signals like 'SIGSTOP', this method
    instead determines it was suspended if it hasn't updated the variable 'last'
    in the last 3 seconds. Note that all time measurements are performed in
    nanoseconds.
    """

    if len(sys.argv) > 1:
        start = int(sys.argv[1])
    else:
        start = 30

    last = time.monotonic_ns()
    for i in range(start, 0, -1):
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
