############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test updating the MODE_3 HRES request of a pending job.

Updating a pending job's licenses must keep the job's MODE_3 HRES check, so
the job must not run while its HRES layer has no capacity left.
"""

import pytest

import atf

# Only leaf1 has capacity, so a job requesting power:2000 fills it and a
# second job must pend until the first ends.
resources_yaml = """
- resource: power
  mode: MODE_3
  layers:
    - layer_name: "root"
      count: 100000
    - layer_name: "leaf1"
      parent_name: "root"
      nodes:
        - "node[1-3]"
      count: 2000
"""

leaf_total = 2000


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Issue #50445: the _update_job() hres_select_t fix is 26.11 only, "
        "and LayerName was added to scontrol show license output in 26.11.",
    )
    atf.require_nodes(3)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_config_file("resources.yaml", resources_yaml)
    atf.require_slurm_running()


def get_leaf1_usage():
    """Return (total, used) for the leaf1 layer of the power HRES."""
    lic = atf.get_licenses(fatal=True)["power(leaf1)"]
    return lic["Total"], lic["Used"]


def fill_leaf1():
    """Start one job that uses the whole capacity of leaf1."""

    job_id = atf.submit_job_sbatch(
        f"-N1 --resources=power:{leaf_total} --mem=1 --wrap='sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    assert get_leaf1_usage() == (
        leaf_total,
        leaf_total,
    ), "The running job should use all of leaf1"
    return job_id


def assert_stays_pending(job_id):
    # With bf_interval=1 and sched_interval=1, a job that is not blocked by
    # the HRES check starts within a few seconds.
    assert not atf.wait_for_job_state(
        job_id, "RUNNING", timeout=5, xfail=True
    ), f"Job {job_id} should not run while leaf1 has no capacity left"
    # Not RUNNING is not the same as still pending: a job that failed or was
    # cancelled also never reaches RUNNING, and would satisfy the wait above.
    assert (
        atf.get_job_parameter(job_id, "JobState") == "PENDING"
    ), f"Job {job_id} should still be PENDING"
    total, used = get_leaf1_usage()
    assert used <= total, f"leaf1 usage ({used}) should not exceed its total ({total})"


def test_update_pending_job():
    """Updating a pending job's HRES request must keep the HRES check"""

    running_job_id = fill_leaf1()

    job_id = atf.submit_job_sbatch(
        f"-N1 --resources=power:{leaf_total} --mem=1 --wrap='sleep infinity'",
        fatal=True,
    )
    assert_stays_pending(job_id)

    update_total = leaf_total - 1
    atf.run_command(
        f"scontrol update jobid={job_id} licenses=power:{update_total}",
        fatal=True,
    )
    assert_stays_pending(job_id)
    assert get_leaf1_usage() == (
        leaf_total,
        leaf_total,
    ), "Updating the pending job should not change leaf1 usage"
    assert (
        atf.get_job_parameter(job_id, "Licenses") == f"power:{update_total}"
    ), "Licenses should reflect the update"

    atf.cancel_jobs([running_job_id], fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert get_leaf1_usage() == (
        leaf_total,
        update_total,
    ), f"The updated job should use power:{update_total} on leaf1"
