############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_parallel_launch_srun():
    """Test parallel launch of srun (e.g. "srun srun id")"""

    mult = 4
    task_cnt = 4
    node_cnt = 1-4
    output = atf.run_job_output(f"-N{node_cnt} -n{task_cnt} -O -t1 --overlap srun -l -n{mult} -O --overlap id")
    match = re.findall(r'\d+: uid=', output)
    assert len(match) == mult * task_cnt, f"Failed to get output from all tasks. Got {len(match)} ouputs expected {task_cnt * mult}"
