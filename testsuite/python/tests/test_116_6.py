############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope='module', autouse=True)
def setup():
    atf.require_config_parameter('FrontendName', None)
    atf.require_config_parameter_excludes('LaunchParameters', 'test_exec')
    atf.require_slurm_running()


def test_running_non_existent_job():
    """Test of running non-existent job, confirm timely termination."""

    # Submit a slurm job that will execute bogus job name
    output_error = atf.run_command_error("srun -t1 /bad/bad/bad", timeout=120, xfail=True)
    no_file = re.findall("No such file", output_error)
    unable_to_run = re.findall("Unable to run executable", output_error)
    time_limit = re.findall("time limit exceeded", output_error)
    terminated = re.findall('Terminated', output_error)

    assert len(time_limit) <= 0, "srun time limit exceeded"
    assert len(terminated) <= 0, "srun did not terminate properly"
    assert len(no_file) > 0 or len(unable_to_run) > 0, "Unexpected output from bad job name"

