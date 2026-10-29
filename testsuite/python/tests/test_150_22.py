############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Backfill must plan an --exclusive=topo job in a block with a down node.

Only backfill schedules here. A running job keeps the block busy, so the whole
topo job gets a future start. The down node is in no backfill map record. If
the whole block bitmap includes it, the bitmap always overlaps and the job is
never planned, so a lower priority job can backfill into the block.
"""

import datetime

import pytest

import atf

part_name = "test_150_22"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        reason="Ticket 25937: whole topo backfill plan with a down node fixed in 26.11",
    )
    nodes = atf.require_nodes(4, [("CPUs", 1)])[:4]
    node_list = ",".join(nodes)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_file(
        "topology.yaml",
        f"""
- topology: block_topo
  cluster_default: true
  block:
    block_sizes:
      - 4
    blocks:
      - block: b1
        nodes: {node_list}
""",
    )
    atf.require_config_parameter("PartitionName", {part_name: {"Nodes": node_list}})
    # Keep the main scheduler and submit time allocation out of the way.
    atf.require_config_parameter_includes("SchedulerParameters", "defer")
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", -1))
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    # Jobs below bf_min_prio_reserve get no plan; a coarse bf_resolution
    # rounds the plan of job B over job D.
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_min_prio_reserve", 1)
    )
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_resolution", 60))
    atf.require_slurm_running()

    return nodes


def _job_time(job_dict, field):
    if job_dict[field] == "Unknown":
        return None
    return datetime.datetime.strptime(job_dict[field], "%Y-%m-%dT%H:%M:%S")


def test_whole_topo_plan_with_down_node(setup):
    nodes = setup
    atf.run_command(
        f"scontrol update nodename={nodes[3]} state=down reason={part_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    job_a = atf.submit_job_sbatch(
        f'-p {part_name} -w {nodes[0]} -t 5 --wrap "sleep infinity"', fatal=True
    )
    atf.wait_for_job_state(job_a, "RUNNING", fatal=True)

    job_b = atf.submit_job_sbatch(
        f'-p {part_name} --exclusive=topo -N1 -t 5 --wrap "sleep infinity"',
        fatal=True,
    )
    # Ends after job A, so it can only start now on the block planned for B.
    job_c = atf.submit_job_sbatch(
        f'-p {part_name} --nice=100 -N1 -t 10 --wrap "sleep infinity"',
        fatal=True,
    )

    # Backfill clears pending start times at the start of every cycle, so read
    # both jobs from one snapshot.
    for _ in atf.timer():
        jobs = atf.get_jobs(env_vars="SLURM_TIME_FORMAT=standard", quiet=True)
        start_b = _job_time(jobs[job_b], "StartTime")
        start_c = _job_time(jobs[job_c], "StartTime")
        if start_b and (start_c or (jobs[job_c]["JobState"] != "PENDING")):
            break
    else:
        assert False, (
            f"Job {job_b} ({start_b}) and pending job {job_c} ({start_c}) "
            "should both get a start time"
        )

    assert (
        jobs[job_b]["JobState"] == "PENDING"
    ), f"Job {job_b} should be PENDING but was {jobs[job_b]['JobState']}"
    assert (
        jobs[job_c]["JobState"] == "PENDING"
    ), f"Job {job_c} should be PENDING but was {jobs[job_c]['JobState']}"
    assert start_c > start_b, (
        f"Job {job_c} should start after job {job_b} ({start_b}) but starts "
        f"at {start_c}"
    )
    end_a = _job_time(jobs[job_a], "EndTime")
    assert start_b >= end_a, (
        f"Job {job_b} should start after job {job_a} ends ({end_a}) but "
        f"starts at {start_b}"
    )

    # Ends before job A, so the plan for job B must not keep it out.
    job_d = atf.submit_job_sbatch(
        f'-p {part_name} --nice=100 -N1 -t 2 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_d, "RUNNING"
    ), f"Job {job_d} should be backfilled before job {job_b} starts"
