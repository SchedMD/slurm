############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

import logging
import re

resources_yaml = """
- resource: power
  mode: MODE_3
  variables:
    - name: full_node
      value: 2000
    - name: full_gpu_node
      value: 5000
  layers:
    - nodes:
        - "node[1-8]"
      count: 40000
      base:
        - name: storage
          value: 5000
    - nodes:
        - "node[9-16]"
      count: 40000
    - nodes:
        - "node[17-24]"
      count: 40000
    - nodes:
        - "node[25-32]"
      count: 60000
    - nodes:
        - "node[1-16]"
      count: 60000
      base:
        - name: network1
          value: 3000
    - nodes:
        - "node[17-32]"
      count: 80000
      base:
        - name: network2
          value: 2000
    - nodes:
        - "node[1-32]"
      count: 130000
      base:
        - name: acUnit1
          value: 10000
        - name: acUnit2
          value: 8000
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((25, 11), component="bin/sacctmgr")
    atf.require_nodes(32)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    atf.add_config_parameter_value(
        "SchedulerParameters", "bf_interval=1,sched_interval=1"
    )

    atf.require_config_file("resources.yaml", resources_yaml)

    atf.require_slurm_running()


def test_const1():
    """Test vars"""

    job_id = atf.submit_job_sbatch(
        '-N 4 --exclusive --resources=power:full_gpu_node --mem=1 --wrap="hostname"'
    )
    assert job_id != 0, "Job should be accepted with a valid var"
    assert atf.wait_for_job_state(job_id, "DONE"), "Job should run with a valid var"

    assert (
        atf.submit_job_sbatch(
            '-N 4 --exclusive --resources=power:foo --mem=1 --wrap="hostname"',
            xfail=True,
        )
        == 0
    ), "Job should fail -- invalid var "


def test_enforce1():
    """Test one level enforcing"""

    job_id = atf.submit_job_sbatch(
        '-N 4 --exclusive --resources=power:10000 -w node[9-12] --mem=1 --wrap="hostname"'
    )
    assert job_id != 0, "Job should be accepted with power value at one level"
    assert atf.wait_for_job_state(
        job_id, "DONE"
    ), "Job should run with power value at one level"

    assert (
        atf.submit_job_sbatch(
            '-N 4 --exclusive --resources=power:10000  -w node[1-4] --mem=1 --wrap="sleep 20"',
            xfail=True,
        )
        == 0
    ), "Job should fail when not enough power on node[1-8] layer"


def test_enforce2():
    """Test multiple levels enforcing"""

    job_id = atf.submit_job_sbatch(
        '-N 10 --exclusive --resources=power:10000 --mem=1 --wrap="hostname"'
    )
    assert job_id != 0, "Job should be accepted with power value at two levels"
    assert atf.wait_for_job_state(
        job_id, "DONE"
    ), "Job should run with power value at two levels"

    assert (
        atf.submit_job_sbatch(
            '-N 11 --exclusive --resources=power:10000 --mem=1 --wrap="sleep 20"',
            xfail=True,
        )
        == 0
    ), "Job should fail when not enough power on node[1-32] layer"


def test_sched1():
    """Test multiple levels enforcing"""
    job_power_per_node = 10000
    job_id = atf.submit_job_sbatch(
        f'-N 10 --exclusive --resources=power:{job_power_per_node} --mem=1 --wrap="hostname"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    job_nodelist = set(
        atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    )
    logging.info(f"NodeList:{job_nodelist}")

    output = atf.run_command_output("scontrol -o show license power", fatal=True)
    matches = re.findall(r"Total=(\d+).*?Nodes=([\w\[\]-]+)", output)
    power_layers = [
        (set(atf.node_range_to_list(nodeset)), int(total)) for total, nodeset in matches
    ]
    logging.info(f"Power:{power_layers}")
    for layer_nodes, layer_total in power_layers:
        overlapping_nodes = job_nodelist.intersection(layer_nodes)
        if overlapping_nodes:
            job_usage_on_layer = len(overlapping_nodes) * job_power_per_node
            assert (
                job_usage_on_layer <= layer_total
            ), f"Job usage ({job_usage_on_layer}) on layer {layer_nodes} should not be bigger than total on layer ({layer_total})"
