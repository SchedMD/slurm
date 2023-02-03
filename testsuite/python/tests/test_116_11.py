############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_immediate():

    # Spawn a srun immediate execution job with hold (priority==0) option,
    # The job can't run immediately with a priority of zero
    run_error = atf.run_command_error("srun --immediate --hold pwd")
    assert re.search(r'Unable to allocate resources', run_error) is not None

    # test that --immediate runs in under 2 seconds
    assert atf.run_command_exit("srun --immediate pwd", timeout=2) == 0
