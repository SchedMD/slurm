############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Per-partition priority array is cleared when a job is held (ticket 25566)."""

import os

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
user_name = atf.properties["test-user"]
high_part = f"{test_name}_high"
low_part = f"{test_name}_low"
node_name = None
node_cpus = 4


@pytest.fixture(scope="module", autouse=True)
def setup():
    global node_name, node_cpus

    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 25566: per-partition priority reporting",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_nodes(1, [("CPUs", node_cpus), ("RealMemory", 1)])
    # Two partitions with distinct job factors so each requested partition
    # gets its own non-zero priority, which is what the per-partition array
    # reports and what this test watches collapse to zero on a hold.
    atf.require_config_parameter(
        "PartitionName",
        {
            high_part: {
                "Nodes": "ALL",
                "State": "UP",
                "OverSubscribe": "NO",
                "PriorityJobFactor": 100,
            },
            low_part: {
                "Nodes": "ALL",
                "State": "UP",
                "OverSubscribe": "NO",
                "PriorityJobFactor": 1,
            },
        },
    )
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    # The partition is the only thing the priority depends on.
    atf.require_config_parameter("PriorityWeightPartition", 1000000)
    atf.require_config_parameter("PriorityWeightAge", 0)
    atf.require_config_parameter("PriorityWeightAssoc", 0)
    atf.require_config_parameter("PriorityWeightFairshare", 0)
    atf.require_config_parameter("PriorityWeightJobSize", 0)
    atf.require_config_parameter("PriorityWeightQOS", 0)
    atf.require_config_parameter("PriorityWeightTRES", None)
    # Keep periodic priority recalculation frequent so the array is built fast.
    atf.require_config_parameter("PriorityCalcPeriod", 1)
    atf.require_slurm_running()

    for name, node in sorted(atf.get_nodes().items()):
        if node["effective_cpus"] >= node_cpus:
            node_name = name
            node_cpus = node["effective_cpus"]
            break
    assert node_name is not None, "Need a node with at least four effective CPUs"


def per_partition_priorities(job_id):
    """Return {partition: PriorityLong} from squeue's per-partition rows.

    With --priority, squeue prints one row per requested partition for a
    pending job, each carrying that partition's value from priority_array.
    """
    output = atf.run_command_output(
        f"squeue --priority -h -j {job_id} -O Partition,PriorityLong", fatal=True
    )
    priorities = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2:
            priorities[fields[0]] = int(fields[1])
    return priorities


@pytest.fixture(scope="function")
def pending_multi_part_job():
    atf.cancel_all_jobs(quiet=True)

    # Fill every CPU so the reservation-bound job stays pending and the
    # multifactor plugin has a pending job to build the array for.
    filler = atf.submit_job_sbatch(
        f"-p {high_part} --nodelist={node_name} -n{node_cpus} --mem=0 "
        f"-t10 --output=/dev/null --wrap='sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(filler, "RUNNING", fatal=True)

    # IGNORE_JOBS so the reservation coexists with the running filler.
    atf.run_command(
        f"scontrol create reservation ReservationName={test_name} "
        f"starttime=now duration=1 flags=IGNORE_JOBS user={user_name} nodes=ALL",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    job_id = atf.submit_job_sbatch(
        f"--reservation={test_name} -p {high_part},{low_part} -n1 --mem=0 "
        f"-t10 --output=/dev/null --wrap='sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)

    yield job_id

    atf.cancel_all_jobs(quiet=True)
    atf.run_command(
        f"scontrol delete ReservationName={test_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_resv_end_clears_priority_array(pending_multi_part_job):
    """Ticket 25566: ending a reservation must zero a job's per-partition array.

    A job held through scontrol already cleared prio_mult->priority_array, but a
    job held because its reservation was deleted went through job_mgr_hold_set()
    without clearing it, so squeue --priority kept reporting the pre-hold
    per-partition priorities while the scalar priority read zero. Build the
    array on a pending multi-partition job, let its reservation end, and assert
    every per-partition priority collapses to zero.
    """
    job_id = pending_multi_part_job

    # The array is built once the per-partition priorities show up non-zero.
    for _ in atf.timer(fatal=True):
        before = per_partition_priorities(job_id)
        if before.get(high_part, 0) > 0 and before.get(low_part, 0) > 0:
            break
    assert (
        before[high_part] > before[low_part]
    ), f"the higher-factor partition should outrank the other: {before}"

    # Let the reservation run out; the bound job is then held with
    # WAIT_RESV_DELETED through job_mgr_hold_set(). Deleting the reservation is
    # refused while a job is bound to it, so the one-minute duration has to
    # expire on its own.
    for _ in atf.timer(timeout=150, poll_interval=2, fatal=True):
        if atf.get_job_parameter(job_id, "Priority") == 0:
            break

    assert (
        atf.get_job_parameter(job_id, "JobState") == "RESV_DEL_HOLD"
    ), "the job must be held because its reservation was deleted"
    after = per_partition_priorities(job_id)
    assert after, "a pending multi-partition job should still report per-partition rows"
    assert all(
        value == 0 for value in after.values()
    ), f"a held job must report zero for every partition, got {after}"
