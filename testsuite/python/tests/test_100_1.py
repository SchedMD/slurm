############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
from pathlib import Path

# Create a test_function() for all test file in the testsuite_check_dir.
# All the test_function()s will be run by pytest as if actually defined here.

testsuite_check_dir = Path(atf.properties["testsuite_check_dir"])
test_files = list(testsuite_check_dir.rglob("test_*.c"))

for test in test_files:
    src = Path(test).relative_to(testsuite_check_dir)
    test_name = "test_" + str(src).replace(".c", "").replace("test_", "").replace(
        ".", "_"
    ).replace("-", "_").replace("/", "_")

    def make_test(src):
        def test_func():
            atf.run_check_test(str(src))

        return test_func

    globals()[test_name] = make_test(src)
