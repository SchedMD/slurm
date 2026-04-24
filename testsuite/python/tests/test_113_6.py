############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import atf
import pytest
import re


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5), "bin/sprio", reason="Ticket 22180: SLUID availability added in 26.05+"
    )
    atf.require_accounting()
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def default_partition():
    return atf.default_partition()


@pytest.fixture(scope="function")
def queued_job(default_partition):
    nodes_dict = atf.get_nodes()
    total_cpus = 0

    for node_name in nodes_dict:
        node_dict = nodes_dict[node_name]
        node_partitions = node_dict["partitions"]
        if default_partition in node_partitions and "IDLE" in node_dict["state"]:
            total_cpus += node_dict["cpus"]

    atf.submit_job_sbatch(
        f'--output=/dev/null --error=/dev/null -n {total_cpus} --exclusive --wrap="sleep infinity"',
        fatal=True,
    )
    queued_job_id = atf.submit_job_sbatch(
        f'--output=/dev/null --error=/dev/null -n {total_cpus} --exclusive --wrap="sleep infinity"',
        fatal=True,
    )

    return queued_job_id


def test_sprio_sluid(queued_job, default_partition):
    """Verify sprio can query by SLUID, filter, and print with %s option."""

    sluid = atf.get_job_parameter(queued_job, "SLUID")
    assert sluid is not None, f"Job {queued_job} has no SLUID"

    fmt = "--noheader -o '%.6i %.14s'"

    # sprio -j <job_id> prints the SLUID with %s
    output = atf.run_command_output(f"sprio -j {queued_job} {fmt}", fatal=True)
    assert re.search(
        rf"{queued_job}\s+{re.escape(sluid)}", output
    ), f"Expected job_id and SLUID in output: {output}"

    # sprio -j <SLUID> filters by SLUID
    output = atf.run_command_output(f"sprio -j {sluid} {fmt}", fatal=True)
    assert re.search(
        rf"{queued_job}\s+{re.escape(sluid)}", output
    ), f"Expected job found by SLUID in output: {output}"

    # sprio -j <job_id>,<SLUID> (both refer to the same job)
    output = atf.run_command_output(f"sprio -j {queued_job},{sluid} {fmt}", fatal=True)
    assert re.search(
        rf"{queued_job}\s+{re.escape(sluid)}", output
    ), f"Expected job found by mixed ids in output: {output}"
