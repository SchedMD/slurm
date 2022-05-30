############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import pexpect


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter('OverTimeLimit', 0)
    atf.require_slurm_running()


def test_debugger_test():
    """Validate Slurm debugger infrastructure (--debugger-test option)."""

    node_count = '1-2'
    task_count = 4
    time_out = 1
    ctld_poll = 60
    wait_time = time_out * 60 + ctld_poll
    child = pexpect.spawn(f"srun -N{node_count} -n{task_count} -O -t{time_out} --debugger-test id")
    response_count = 0
    while response_count < task_count:
        child.expect('host:')
        response_count += 1
    child.expect('TIME LIMIT', timeout=wait_time)
