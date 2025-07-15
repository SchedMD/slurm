############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
from pathlib import Path

xfail_tests = []
skip_tests = []
"""
List for xfails and skips to append known issues (as code).

The xfail_tests is of tuples of:
- file name relative to testsuite_check_dir
- test case name used tcase_add_test(), or None for compilation xfailures
- referenced reason of the known issue

The skip_testis list is equivalent, but without the test case name because
the whole test file won't be compiled nor run.

e.g.

To xfail specific test case in a test file:

xfail_tests.append(
   (
       "common/test_timespec_t.c",
       "test_normalize",
       "Issue #50096. Math operations fail cross negatives",
   )
)

Or to xfail the whole test file:

xfail_tests.append(
   (
       "common/test_timespec_t.c",
       None,
       "Issue #NNNN. Test don't compile due OS issue",
   )
)

Or to skip a test:

skip_tests.append(
   (
       "common/test_timespec_t.c",
       "Feature available in versions 24.11+",
   )
)

Note that xfails will run normally and when they fail we'll get the reason in
the output. If they pass we'll get notified as "failure", so we can check them
and hopefully remove them from the xfail list.
"""

# TODO: Remove xfail_tests.append() once their issue is fixed.
if atf.get_version() < (25, 11):
    skip_tests.append(
        (
            "interfaces/test_data_parsers.c",
            "test_data_parsers doesn't compile for versions < 25.11",
        )
    )
if atf.get_version() < (25, 5):
    skip_tests.append(
        (
            "common/test_timespec_t.c",
            "timespec_t test doesn't compile for versions < 25.05",
        )
    )
    skip_tests.append(
        (
            "common/test_conmgr.c",
            "conmgr test doesn't compile for versions < 25.05",
        )
    )
else:
    xfail_tests.append(
        (
            "common/test_timespec_t.c",
            "test_normalize",
            "Issue #50096. Math operations fail cross negatives",
        )
    )

# Create a test_function() for all test file in the testsuite_check_dir.
# All the test_function()s will be run by pytest as if actually defined here.

testsuite_check_dir = Path(atf.properties["testsuite_check_dir"])
test_files = list(testsuite_check_dir.rglob("test_*.c"))

for test in test_files:
    src = str(Path(test).relative_to(testsuite_check_dir))
    test_name = "test_" + src.replace(".c", "").replace("test_", "").replace(
        ".", "_"
    ).replace("-", "_").replace("/", "_")

    def make_test(src):

        def test_func():
            if atf.is_upgrade_setup():
                pytest.skip("The libcheck test doesn't work with upgrade setups")

            # Run the libcheck tests and get the xml parsed results
            test_results = atf.run_check_test(src)

            # Get a list of test cases (ensure it's a list)
            test_cases = test_results["test"]
            if not isinstance(test_cases, list):
                test_cases = [test_cases]

            # Get the list of xfail for this suite (if any)
            suite_xfails = [xfail for xfail in xfail_tests if xfail[0] == src]

            # Get the failure in the suite and evaluate if they are xfail or we should fail
            suite_fails = [test for test in test_cases if test["@result"] != "success"]
            xfails_found = []
            for failure in suite_fails:
                for i, xfail in enumerate(suite_xfails):
                    if xfail[1] == failure["id"]:
                        # failure is an xfail
                        suite_xfails.pop(i)
                        xfails_found.append(xfail)
                        break
                else:
                    # failure is not xfail
                    pytest.fail(f"{failure['id']}: {failure['message']}")

            # Check if we still have remaining xfails (not popped above)
            for xfail in suite_xfails:
                xpass = next(
                    (test for test in test_results["test"] if test["id"] == xfail[1]),
                    None,
                )
                if xpass:
                    pytest.fail(
                        f"{xfail[1]} is marked as xfail but {xpass['@result']}: {xpass['message']}"
                    )
                else:
                    pytest.fail(f"{xfail[1]} is marked as xfail but never run")

            # Finally, if some xfail failed, mark the test as xfail
            xfail_msg = ""
            for xfail in xfails_found:
                xfail_msg += f"{xfail[1]}: {xfail[2]}\n"
            if xfail_msg != "":
                pytest.xfail(xfail_msg)

        # If there's an xfail for the test file (test_case is None), always xfail
        test_file_xfails = [
            xfail for xfail in xfail_tests if xfail[0] == src and xfail[1] is None
        ]

        if test_file_xfails:
            test_func = pytest.mark.xfail(reason=test_file_xfails[0][2])(test_func)

        # Skip the test if listed
        test_file_skip = [skip for skip in skip_tests if skip[0] == src]

        if test_file_skip:
            test_func = pytest.mark.skip(reason=test_file_skip[0][1])(test_func)

        return test_func

    globals()[test_name] = make_test(src)
