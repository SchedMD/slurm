############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

node_num = 4

# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()
    atf.require_nodes(node_num)

def test_threads():
    """Verify a job executes with various launch thread fanouts with --threads option"""

    node_count = "1-64"
    threads = 0
    output = atf.run_command_output(f"srun -N{node_count} -l --threads=1 printenv SLURMD_NODENAME")
    assert re.search(r"0: (\S+)", output) is not None, f"Did not get hostname of task 0"

    task_num = int(atf.get_config_parameter("MaxTasksPerNode"))
    if task_num <= 32:
        threads = task_num
    else:
        threads = 32
    task_num = threads * 2
    output = atf.run_command(f"srun -N{node_count} -n{task_num} -O -l --threads={threads} printenv SLURMD_NODENAME")
    assert output['exit_code'] == 0 , f'srun failed to run with {threads} threads and {task_num} tasks'
    assert re.search(r"0: (\S+)", output['stdout']) is not None, f"Did not get hostname of task 0 when running {threads} threads"
