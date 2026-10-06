############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test Mode 3 HRES preemption between jobs that share no nodes."""

import pytest

import atf

pytestmark = pytest.mark.slow

# MODE_3 hierarchy where each node has plenty of the "power" resource but the
# root layer only has enough for 100 units in total. Jobs landing on different
# nodes still collide at the root, so a higher-priority job must preempt
# lower-priority ones to run -- even though they share no nodes.
resources_yaml = """
- resource: power
  mode: MODE_3
  layers:
    - layer_name: "leaf1"
      parent_name: "root"
      nodes:
        - "node1"
      count: 100
    - layer_name: "leaf2"
      parent_name: "root"
      nodes:
        - "node2"
      count: 100
    - layer_name: "leaf3"
      parent_name: "root"
      nodes:
        - "node3"
      count: 100
    - layer_name: "leaf4"
      parent_name: "root"
      nodes:
        - "node4"
      count: 100
    - layer_name: "root"
      count: 100
"""


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("modifies preemption and partition configuration")
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Mode 3 HRES preemption was added in 26.11",
    )
    atf.require_version(
        (25, 11),
        component="bin/sbatch",
        reason="--resources was added in 25.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    atf.require_config_parameter("PreemptMode", "CANCEL")
    # Keep completed job records so the preempted job stays queryable
    atf.require_config_parameter("MinJobAge", 3600)
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    # Pin the preemptee ordering so test_mode3_prune_unneeded_preemptee can
    # predict which candidates are removed.
    atf.require_config_parameter_includes("PreemptParameters", "strict_order")
    atf.require_config_parameter_includes("PreemptParameters", ("reorder_count", 0))
    atf.require_nodes(4, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    # With preempt/partition_prio the preemptee's partition PreemptMode wins
    # over the cluster one, so each mode gets its own partition instead of
    # reconfiguring between cases.
    atf.require_config_parameter(
        "PartitionName",
        {
            "lowcancel": {
                "Nodes": "ALL",
                "PriorityTier": "1",
                "PreemptMode": "CANCEL",
            },
            "lowrequeue": {
                "Nodes": "ALL",
                "PriorityTier": "1",
                "PreemptMode": "REQUEUE",
            },
            "lowcancel2": {
                "Nodes": "ALL",
                "PriorityTier": "2",
                "PreemptMode": "CANCEL",
            },
            "lowcancel3": {
                "Nodes": "ALL",
                "PriorityTier": "3",
                "PreemptMode": "CANCEL",
            },
            "highprio": {"Nodes": "ALL", "PriorityTier": "4"},
        },
    )
    atf.require_config_file("resources.yaml", resources_yaml)

    atf.require_slurm_running()


@pytest.mark.parametrize(
    "partition,preempted_state",
    [
        ("lowcancel", "PREEMPTED"),
        ("lowrequeue", "PENDING"),
    ],
)
def test_mode3_preempt(partition, preempted_state):
    """Verify MODE_3 HRES preemption without node overlap.

    A low-priority job on node1 consumes the shared root-layer "power"
    budget. A higher-priority job requesting the same resource on node2
    cannot start until the low-priority job is preempted, even though the
    two jobs share no nodes.
    """

    low_id = atf.submit_job_sbatch(
        f"-p {partition} -w node1 --resources=power:100 --mem=1 -o /dev/null"
        ' --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        low_id, "RUNNING"
    ), f"Low-priority job ({low_id}) did not start"

    high_id = atf.submit_job_sbatch(
        "-p highprio -w node2 --resources=power:100 --mem=1 -o /dev/null"
        ' --wrap "sleep infinity"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        low_id, preempted_state
    ), f"Low-priority job ({low_id}) was not preempted ({preempted_state})"

    assert atf.wait_for_job_state(
        high_id, "RUNNING"
    ), f"High-priority job ({high_id}) did not start after preemption"

    atf.cancel_jobs([low_id, high_id], fatal=True)


def test_mode3_prune_unneeded_preemptee():
    """Verify a preemptee MODE_3 does not need is left running.

    The root layer holds 100 units of "power", spread over three
    lower-priority jobs: 20 on node1, 40 on node2 and 40 on node3. A
    high-priority job wants 80 on node4, so freeing the two 40-unit jobs
    is enough and the 20-unit job never has to die.

    Candidates are tried in preemptee order, which is 20, 40, 40 here. The
    first pass has to remove all three before the job fits, and the reorder
    pass only moves the last one to the front, so the 20-unit job is still
    in the list that gets built. Removing it again is the job of the second
    pass over the preemptee list.
    """

    keep_id = atf.submit_job_sbatch(
        "-p lowcancel -w node1 --resources=power:20 --mem=1 -o /dev/null"
        ' --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        keep_id, "RUNNING"
    ), f"Job ({keep_id}) holding 20 units did not start"

    victim_ids = []
    for partition, node in (("lowcancel2", "node2"), ("lowcancel3", "node3")):
        job_id = atf.submit_job_sbatch(
            f"-p {partition} -w {node} --resources=power:40 --mem=1 -o /dev/null"
            ' --wrap "sleep infinity"',
            fatal=True,
        )
        assert atf.wait_for_job_state(
            job_id, "RUNNING"
        ), f"Job ({job_id}) holding 40 units did not start"
        victim_ids.append(job_id)

    high_id = atf.submit_job_sbatch(
        "-p highprio -w node4 --resources=power:80 --mem=1 -o /dev/null"
        ' --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        high_id, "RUNNING"
    ), f"High-priority job ({high_id}) did not start after preemption"

    for job_id in victim_ids:
        assert atf.wait_for_job_state(
            job_id, "PREEMPTED"
        ), f"Job ({job_id}) holding 40 units was not preempted"

    assert (
        atf.get_job_parameter(keep_id, "JobState") == "RUNNING"
    ), f"Job ({keep_id}) holding 20 units was preempted but was not needed"

    atf.cancel_jobs([keep_id, high_id] + victim_ids, fatal=True)
