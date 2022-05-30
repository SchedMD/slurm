############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import os

total_cores = 0
available_cores = 0


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to set CoreSpecCount on a node")
    atf.require_config_parameter('SelectType', 'select/cons_res')
    atf.require_config_parameter('CoreSpecPlugin', 'core_spec/none')
    atf.require_config_parameter('TaskPlugin', 'task/cgroup')
    atf.require_config_parameter('AllowSpecResourcesUsage', '1')
    atf.require_nodes(1, [('Cores', 2)])
    atf.require_config_parameter('ConstrainCores', 'yes', source='cgroup')
    atf.require_slurm_running()


# Assumes default value of ThreadsPerCore=1
@pytest.fixture(scope="module")
def node_name():
    global total_cores, available_cores
    node = atf.run_job_nodes(f"--cpu-bind=core -N1 -n2 true")[0]
    atf.set_node_parameter(node, 'CoreSpecCount', 1)
    sockets = atf.get_node_parameter(node, 'Sockets')
    cores_per_socket = atf.get_node_parameter(node, 'CoresPerSocket')

    total_cores = sockets * cores_per_socket
    available_cores = total_cores - 1
    return node


def test_job_submit(node_name):
    """Verify a properly formed job submits with CoreSpecCount plugin enabled"""

    exit_code = atf.run_job_exit(f"-w {node_name} -N1 -n{available_cores} true")
    assert exit_code == 0


def test_job_denied(node_name):
    """Verify a malformed job is rejected with CoreSpecCount plugin enabled"""

    # Need to add a -N1 to restrict it to the node as it was decided that slurm would override
    # the conf when the ThreadsPerCore=1 doesn't match the hardware and thus allows multiple
    # tasks on a core to go through.

    # See Bug 9754, ~ comment 24: https://bugs.schedmd.com/show_bug.cgi?id=10613#c24

    exit_code = atf.run_job_exit(f"-w {node_name} -N1 -n{total_cores} true")
    assert exit_code != 0


def test_AllowSpecResourcesUsage(node_name):
    """Verify AllowSpecResourcesUsage override functionality works"""

    exit_code = atf.run_job_exit(f" -w {node_name} -N1 --core-spec=0 -n{total_cores} true")
    assert exit_code == 0
