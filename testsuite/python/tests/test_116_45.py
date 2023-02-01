############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

sleep_time = 90
kill_wait =  30


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("OverTimeLimit", 0)
    atf.require_config_parameter("KillWait", kill_wait)
    atf.require_config_parameter("InactiveLimit", sleep_time)
    atf.require_slurm_running()


def test_t():
    """Verify srun job time limit function works (-t option)"""

    # Execute a couple of three minute jobs; one with a one minute time
    # limit and the other with a four minute time limit. Confirm jobs
    # are terminated on a timeout as required. Note that Slurm time
    # limit enforcement has a resolution of about one minute.
    #
    # Ideally the job gets a "job exceeded time limit" followed by a
    # "Terminated" message, but if the timing is bad only the "Terminated"
    # message gets sent. This is due to srun recognizing job termination
    # prior to the message from slurmd being processed.

    timeout = sleep_time + kill_wait
    error = atf.run_command_error(f"srun -t1 sleep {sleep_time}", timeout=timeout, xfail=True)
    assert re.search(r'time limit|terminated', error, re.IGNORECASE) is not None

    error = atf.run_command_error(f"srun -t4 sleep {sleep_time}", timeout=timeout)
    assert not (re.search(r'time limit|terminated', error, re.IGNORECASE)) is not None
