############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import sys
from math import floor

COL1 = 48
COL2 = 14
COL3 = 20

TEST_TXT_FMT = f"{{:<{COL1}s}}{{:>{COL2}s}}{{:<{COL3}s}}"


class Colors:
    # 8
    BLK = "\u001b[30m"
    RED = "\u001b[31m"
    GREEN = "\u001b[32m"
    YELLOW = "\u001b[33m"
    BLUE = "\u001b[34m"
    MAGENTA = "\u001b[35m"
    CYAN = "\u001b[36m"
    WHITE = "\u001b[37m"
    RESET = "\u001b[0m"


CLR_PASS = Colors.CYAN
CLR_FAIL = Colors.RED
CLR_SKIP = Colors.BLUE
CLR_RESET = Colors.RESET


def color_state(state, msg):
    clr = ""

    if state == "FAILED" or state == "FAIL":
        clr = CLR_FAIL

    if state == "PASSED" or state == "PASS":
        clr = CLR_PASS

    if state == "SKIPPED" or state == "SKIP":
        clr = CLR_SKIP

    if state == "WARNING" or state == "WARN":
        clr = CLR_WARN

    return f"{clr}{msg}{CLR_RESET}"


def print_test_line(name, duration, status):
    clr = CLR_FAIL

    if status == "PASSED":
        clr = CLR_PASS
    elif status == "SKIPPED":
        clr = CLR_SKIP

    print(
        TEST_TXT_FMT.format(
            f"{clr}{name}",
            f"{duration} sec ",
            f"{status}{CLR_RESET}",
        )
    )


def print_status_line(
    msg, tests_complete, tests_total, prefix="> Running ", suffix="..."
):
    col1 = 52
    col2 = 5
    col3 = 10

    STATUS_TXT_FMT = f"{{:<{col1}s}}{{:>{col2}s}}{{:<{col3}s}}"
    perc = f"{floor((tests_complete / tests_total) * 100)}% "
    info = f"[{tests_complete}/{tests_total}]"

    print(STATUS_TXT_FMT.format(f"{prefix}{msg}{suffix}", perc, info), end="\r")
