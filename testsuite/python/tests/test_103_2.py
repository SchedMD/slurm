############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import atf
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_slurm_running()


def test_salloc_normal():
    """Test salloc allocations without commands. We test the stderr and
    stdout because the exit_codes seem to be 0 even when it has error messages.
    The normal allocations should have no error output with the -Q flag."""

    # Test that normal salloc works correctly
    result = atf.run_command("salloc -Q true")
    assert result["exit_code"] == 0, "Exit code was not 0!"
    assert (
        result["stderr"] == ""
    ), f"There should be no error messages from the salloc command. Got: {result['stderr']}"
    assert (
        result["stdout"] == ""
    ), f"There should be no stdout from the salloc command. Got: {result['stdout']}"

    atf.cancel_all_jobs()

    # Test that salloc -n1 works correctly
    result = atf.run_command("salloc -Q -n1 true")
    assert result["exit_code"] == 0, "Exit code was not 0!"
    assert (
        result["stderr"] == ""
    ), f"There should be no error messages from the salloc command. Got: {result['stderr']}"
    assert (
        result["stdout"] == ""
    ), f"There should be no stdout from the salloc command. Got: {result['stdout']}"

    atf.cancel_all_jobs()

    # Test that salloc -n2 is rejected with only one node
    result = atf.run_command("salloc -Q -n2 true", timeout=3)
    assert result["exit_code"] != 0, "Verify salloc failed"
    assert re.search(
        "Requested node configuration is not available", result["stderr"]
    ), f"Error message should contain 'Requested node configuration is not available'. Got: {result['stderr']}"
    assert (
        result["stdout"] == ""
    ), f"There should be no stdout from the salloc command. Got: {result['stdout']}"
