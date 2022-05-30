############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import logging
import re
import os
import pathlib
import pytest
import subprocess
import sys

# test_id is parametrized in conftest.py based on the --include-expect option
def test_expect(test_id):
    """Run parametrized expect test"""

    test_name = f"test{test_id}"
    logging.info(f"Executing expect {test_name}")
    expect_test_dir = pathlib.Path(__file__).resolve().parents[2] / 'expect'
    test_path = str(expect_test_dir / test_name)
    cp = subprocess.run(test_path, capture_output=True, text=True, cwd=expect_test_dir)

    print(cp.stdout, file=sys.stdout, end='')
    print(cp.stderr, file=sys.stderr, end='')

    if cp.returncode == 0:
        return

    elif cp.returncode > 127:
        warnings = re.findall(r'(?sm)^\[[^\]]*\] *Warning *(.*?) *\([^\)]*\)$', cp.stdout)
        if warnings:
            pytest.skip(warnings[0].replace('\n', ' '))
            #from _pytest.outcomes import Skipped
            #raise Skipped(msg=warnings[0].replace('\n', ' '))
            #raise Skipped()
        else:
            pytest.skip("No warning message detected")

    else:
        fatals = re.findall(r'(?sm)^\[[^\]]*\] *Fatal *(.*?) *\([^\)]*\)$', cp.stdout)
        errors = re.findall(r'(?sm)^\[[^\]]*\] *Error *(.*?) *\([^\)]*\)$', cp.stdout)
        if fatals:
            pytest.fail(fatals[0].replace('\n', ' '))
            #from _pytest.outcomes import Failed
            #from _pytest.outcomes import OutcomeException
            #raise OutcomeException(msg=fatals[0].replace('\n', ' '), pytrace=False)
        elif errors:
            pytest.fail(errors[0].replace('\n', ' '))
        else:
            pytest.fail("No fatal or error message detected")
