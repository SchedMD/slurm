############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import atf
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_config_parameter("EnforcePartLimits", "ALL")
    atf.require_slurm_running()


@pytest.mark.xfail(
    atf.get_version() < (24, 11, 1)
    and "use_interactive_step"
    in atf.get_config_parameter("LaunchParameters", "", live=False),
    reason="The 'ioctl(TIOCGWINSZ): Inappropriate ioctl for device' error when using LaunchParameters=use_interactive_step was fixed in 24.11.1",
)
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

    if atf.get_version() >= (25, 5):
        # Test that salloc -n2 is rejected with only one node
        result = atf.run_command("salloc -Q -n2 true", timeout=3)
        assert result["exit_code"] != 0, "Verify salloc failed"
        assert re.search(
            "More processors requested than permitted", result["stderr"]
        ), f"Error message should contain 'More processors requested than permitted'. Got: {result['stderr']}"
        assert (
            result["stdout"] == ""
        ), f"There should be no stdout from the salloc command. Got: {result['stdout']}"
