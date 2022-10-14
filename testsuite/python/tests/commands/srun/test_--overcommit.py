############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(1, [('CPUs', 1)])
    atf.require_slurm_running()


def test_overcommit():
    """Verify that srun --overcommit will run with more tasks than the node has cpus"""

    # Find an idle node with at least 1 cpu
    eligible_node = None
    for node in atf.nodes:
        if atf.nodes[node]['State'] == 'IDLE' and atf.nodes[node]['CPUTot'] > 0:
            eligible_node = node
            cpu_count = atf.nodes[node]['CPUTot']
            break

    if eligible_node is None:
        pytest.skip("This test requires at least one idle node with a cpu")

    results = atf.run_command(f"srun -N 1 -w {eligible_node} -n {cpu_count + 1} --overcommit -v true")

    assert results['exit_code'] == 0
    assert re.search(rf"srun: ntasks\s+: {cpu_count + 1}", results['stderr']) is not None
