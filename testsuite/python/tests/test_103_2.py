############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_salloc_normal():
    """Test salloc allocations without commands. We test the stderr and
    stdout because the exit_codes seem to be 0 even when it has error messages.
    The normal allocations should have no error output with the -Q flag."""

    # Test that normal salloc works correctly
    result = atf.run_command("salloc -Q &")
    assert result["exit_code"] == 0, "Exit code was not 0!"
    assert (
        result["stderr"] == ""
    ), f"There should be no error messages from the salloc command. Got: {result['stderr']}"
    assert (
        result["stdout"] == ""
    ), f"There should be no stdout from the salloc command. Got: {result['stdout']}"

    atf.cancel_all_jobs()

    # Test that salloc -n1 works correctly
    result = atf.run_command("salloc -Q -n1 &")
    assert result["exit_code"] == 0, "Exit code was not 0!"
    assert (
        result["stderr"] == ""
    ), f"There should be no error messages from the salloc command. Got: {result['stderr']}"
    assert (
        result["stdout"] == ""
    ), f"There should be no stdout from the salloc command. Got: {result['stdout']}"

    atf.cancel_all_jobs()

    # Test that salloc -n2 will wait for resources correctly with only one node
    result = atf.run_command("salloc -Q -n2 &", timeout=3)
    assert result["exit_code"] == 110, "Exit code was not 110 (timeout)!"
    assert (
        result["stderr"] == ""
    ), f"There should be no error messages from the salloc command. Got: {result['stderr']}"
    assert (
        result["stdout"] == ""
    ), f"There should be no stdout from the salloc command. Got: {result['stdout']}"
