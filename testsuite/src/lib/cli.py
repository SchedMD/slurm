############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import argparse, re
from collections import OrderedDict


def get_args(argv=None):
    parser = argparse.ArgumentParser(prog="app", description="host", epilog="")

    # TODO add number of recursions, continue, prompt before start

    parser.add_argument(
        "-i",
        "--include",
        dest="include",
        default="unit, expect, python",
        type=str,
        help="include these tests",
    )

    parser.add_argument(
        "-e",
        "--exclude",
        dest="exclude",
        default="",
        type=str,
        help="exclude these tests",
    )

    parser.add_argument(
        "-o",
        "--output",
        dest="output_dir",
        type=str,
        help="where you'd like the logs stored (supply absolute path)",
    )

    parser.add_argument(
        "-r",
        "--resume",
        dest="resume",
        default=False,
        action="store_true",
        help="resume from the last run",
    )

    parser.add_argument(
        "-R",
        "--reset",
        dest="reset",
        default=False,
        action="store_true",
        help="reset the local db",
    )

    return parser.parse_args()


def test_parser(value, unit_tests_list):
    """Function for parsing suite / test lists from args

    Takes a string of suites and / or tests and returns a unique list
    to be used lated.

    If a whole suite is chosen (ie 'expect') and individual tests within
    that suite are chosen as well, the whole suite wins out rather than doing the
    suite (with that test in it) and that test again in isolation.

    Some examples of exceptable options are:

        '1.5'
        'test9.8'
        '2.6 test3.1 14.2'
        '3.4,6.7,8.3'
        '1.*'
        '*.2'
        '1.*,3.8,9.2'
        'expect,unit,python,1.1,111_1,1.*' -> 'expect,unit,python'
    """

    result = OrderedDict()
    result["slurm_unit"] = []
    result["expect"] = []
    result["python"] = []

    # Remove commas
    value = value.replace(",", " ")

    # Split the suites (if any) and remove from value string
    suite_re = re.compile(r"\bunit\b | \bexpect\b | \bpython\b", flags=re.I | re.X)
    chosen_suites = []
    if suites := list(set(suite_re.findall(value))):
        for suite in suites:
            out_val = suite

            # Rename unit suite to slurm_unit to use later
            # (so it matches the dir structure)
            if suite == "unit":
                out_val = "slurm_unit"
                result["slurm_unit"] = []

            result[out_val].append("all")
            chosen_suites.append(suite)
            value = value.replace(suite, "").strip()

    # Split the user's option string into a series of tuples that represent
    # each test, and add each tuple to the destination array.
    if len(value) > 0:
        test_re = re.compile("(?=test)?(\d+|\*)(\.|_)(\d+|\*).*$")
        splitter = re.compile("[,\s]+")
        val = splitter.split(value)

        for v in val:
            matches = test_re.findall(v)

            if len(matches) > 0:
                m = matches[0]

                # expect tests:
                if m[1] == ".":
                    if not "expect" in chosen_suites:
                        result["expect"].append(f"expect/test{''.join(m)}")

                # python tests:
                if m[1] == "_":
                    if not "python" in chosen_suites:
                        result["python"].append(f"python/tests/test_{''.join(m)}.py")

                value = value.replace(v, "")

    # Handle individual unit tests that may exists
    if len(value) > 0 and not "slurm_unit" in chosen_suites:
        val = splitter.split(value)

        for unit_test_name in val:
            if len(unit_test_name) > 0:
                for unit_test_path in unit_tests_list:
                    if unit_test_name in unit_test_path:
                        result["slurm_unit"].append(unit_test_path)

    # Remove duplicates
    for k, v in result.items():
        result[k] = list(set(v))

    return result
