############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter('FrontendName', None)
    atf.require_slurm_running()


def test_ntasks_per_node():
    """Test of --ntasks-per-node option."""

    task_num = 2
    output = atf.run_job_output(f"-N1 --ntasks-per-node={task_num} -O -l id")
    match = re.findall(r'\d+: uid=', output)
    assert len(match) == task_num, f"Failed to get output from all tasks. Got {len(match)} expected {task_num}"
