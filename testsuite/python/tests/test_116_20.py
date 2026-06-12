############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_slurmdDebug():
    # Test info level
    error = atf.run_command_error(
        "srun --slurmd-debug=info true", user=atf.properties["slurm-user"], fatal=True
    )
    assert re.search(r"debug levels are stderr=", error) is None

    # Test verbose level
    error = atf.run_command_error(
        "srun --slurmd-debug=verbose true",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert re.search(r"debug levels are stderr='verbose'", error) is not None
    assert re.search(r"starting 1 task", error) is not None

    # Test debug level
    error = atf.run_command_error(
        "srun --slurmd-debug=debug true", user=atf.properties["slurm-user"], fatal=True
    )
    assert re.search(r"debug levels are stderr='debug'", error) is not None
    assert re.search(r"debug:", error) is not None
