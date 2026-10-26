############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test updating a pending job's memory to its partition's default.

A job that requests no memory inherits the partition's memory limit and keeps
being re-derived from the default of whatever partition it sits in. Asking for
that same value explicitly has to stop that from happening.
"""

import pytest

import atf

slurm_user = atf.properties["slurm-user"]

DEF_MEM_LOW = 100
DEF_MEM_HIGH = 200


@pytest.fixture(
    scope="module",
    params=[
        pytest.param(("DefMemPerCPU", "MinMemoryCPU"), id="per_cpu"),
        pytest.param(("DefMemPerNode", "MinMemoryNode"), id="per_node"),
    ],
    autouse=True,
)
def setup(request):
    # Module scope is safe while parameters only replace reconfigurable
    # PartitionName stanzas. Use a function-scoped indirect fixture if a
    # restart-only requirement is added here.
    config_parameter, job_parameter = request.param

    # Some tests added here should run just fine on other versions, but
    # the major bug fixed here is only addressed in 26.11
    atf.require_version(
        (26, 11),
        reason="Updating a job's memory to the partition default used to be "
        "silently ignored",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Not require_config_parameter_includes: the CR_* values are mutually
    # exclusive alternatives, not an additive list.
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU_Memory")
    atf.require_nodes(2, [("RealMemory", 1000)])
    atf.require_config_parameter(
        "PartitionName",
        {
            "part_low": {
                "Nodes": "ALL",
                "Default": "YES",
                "State": "UP",
                config_parameter: DEF_MEM_LOW,
            },
            "part_high": {
                "Nodes": "ALL",
                "State": "UP",
                config_parameter: DEF_MEM_HIGH,
            },
        },
    )
    atf.require_slurm_running()

    return config_parameter, job_parameter


@pytest.fixture
def job_id(setup):
    """A held job whose memory comes from part_low's default."""

    config_parameter, job_parameter = setup

    job_id = atf.submit_job_sbatch(
        "--hold --nodes=1 --ntasks=1 --partition=part_low " '--wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.get_job_parameter(job_id, job_parameter) == f"{DEF_MEM_LOW}M", (
        f"Job should start out with part_low's {config_parameter} " f"({DEF_MEM_LOW}M)"
    )

    return job_id


def move_to_part_high(job_id):
    """Move the job to part_high, which has a different memory default."""

    atf.run_command(
        f"scontrol update JobId={job_id} Partition=part_high",
        user=slurm_user,
        fatal=True,
    )


def test_default_memory_follows_partition_default(job_id, setup):
    """Verify an untouched memory limit follows the partition default.

    This is the control for test_update_memory: it shows that moving the job to
    part_high does re-derive the limit as long as the job never asked for one
    itself.
    """

    move_to_part_high(job_id)

    config_parameter, job_parameter = setup

    assert atf.get_job_parameter(job_id, job_parameter) == f"{DEF_MEM_HIGH}M", (
        f"A job that never requested memory should pick up part_high's "
        f"{config_parameter} ({DEF_MEM_HIGH}M)"
    )


@pytest.mark.parametrize(
    "requested_memory",
    [
        pytest.param(DEF_MEM_LOW, id="partition_default"),
        pytest.param(DEF_MEM_LOW - 10, id="below_partition_default"),
    ],
)
def test_update_memory(job_id, setup, requested_memory):
    """Verify explicitly updating memory is honored.

    The requested value is identical to the one the job already inherited from
    part_low for the partition_default case, but it is now an explicit request
    and must no longer be re-derived from the default of another partition.
    """

    config_parameter, job_parameter = setup

    atf.run_command(
        f"scontrol update JobId={job_id} {job_parameter}={requested_memory}",
        user=slurm_user,
        fatal=True,
    )

    move_to_part_high(job_id)

    assert atf.get_job_parameter(job_id, job_parameter) == f"{requested_memory}M", (
        f"The requested {requested_memory}M should be kept after moving to "
        f"part_high, not replaced by its {config_parameter}"
    )


def test_update_nodes_does_not_set_memory(job_id, setup):
    """Verify updating nodes does not turn a default into requested memory."""

    atf.run_command(
        f"scontrol update JobId={job_id} NumNodes=2",
        user=slurm_user,
        fatal=True,
    )
    assert (
        atf.get_job_parameter(job_id, "NumCPUs") == 2
    ), "Updating NumNodes should raise the pending job's CPU count to 2"

    move_to_part_high(job_id)

    config_parameter, job_parameter = setup

    assert atf.get_job_parameter(job_id, job_parameter) == f"{DEF_MEM_HIGH}M", (
        "Updating NumNodes must not pin the job to part_low's default memory; "
        f"the job should pick up part_high's {config_parameter}"
    )
