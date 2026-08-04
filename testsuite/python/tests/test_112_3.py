############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Tests for the implicit max_nodes rollback on a rejected node count update."""

import os

import pytest
import requests

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
TASKS = 8
IMPLICIT_MAX_NODES = 8
MIN_NODES = 2
UPDATE_TASKS_PER_NODE = 4
REDERIVED_MAX_NODES = 2


@pytest.fixture(scope="module", autouse=True)
def setup():
    # slurmrestd is upgraded with the client commands, so it is never newer
    # than slurmctld and this also gates the implicit max_nodes support
    atf.require_version(
        (26, 5, 3),
        "sbin/slurmrestd",
        reason="Ticket 25443: data_parser v0.0.45 was added in 26.05 and the implicit max_nodes rederive in 26.05.3",
    )
    atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt")
    atf.require_slurmrestd("slurmctld,util", "v0.0.45")
    atf.require_nodes(2, [("CPUs", 8), ("RealMemory", 1024)])
    atf.require_slurm_running()


def test_rejected_max_nodes_update_keeps_implicit_bound():
    """Verify a rejected node count update leaves the implicit max_nodes intact"""

    job_id = atf.submit_job_sbatch(
        f'--hold -n{TASKS} -J {test_name} -t 1 --wrap "hostname" -o /dev/null',
        fatal=True,
    )
    max_nodes = atf.get_jobs(job_id, use_json=True, fatal=True)[job_id]["max_nodes"][
        "number"
    ]
    assert (
        max_nodes == IMPLICIT_MAX_NODES
    ), f"max_nodes should start implicitly derived as {IMPLICIT_MAX_NODES}"

    # Raising only the minimum leaves max_nodes implicitly derived, which is
    # what lets the next update be rejected by slurmctld
    response = requests.post(
        f"{atf.properties['slurmrestd_url']}/slurm/v0.0.45/job/{job_id}",
        headers=atf.properties["slurmrestd-headers"],
        json={"minimum_nodes": MIN_NODES},
    )
    assert (
        response.status_code == 200
    ), f"Raising minimum_nodes should return HTTP 200, got {response.status_code}"
    errors = response.json().get("errors")
    assert not errors, f"Raising minimum_nodes should succeed: {errors}"

    # slurmrestd can set maximum_nodes without minimum_nodes, which scontrol
    # cannot, so this is the only way to have slurmctld reject the update
    response = requests.post(
        f"{atf.properties['slurmrestd_url']}/slurm/v0.0.45/job/{job_id}",
        headers=atf.properties["slurmrestd-headers"],
        json={"maximum_nodes": MIN_NODES - 1},
    )
    assert response.headers.get("content-type", "").startswith(
        "application/json"
    ), f"Expected a JSON response, got {response.headers.get('content-type')}"
    assert response.json().get(
        "errors"
    ), "A maximum_nodes below the minimum should be rejected"

    atf.run_command(
        f"scontrol update jobid={job_id} TasksPerNode={UPDATE_TASKS_PER_NODE}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    max_nodes = atf.get_jobs(job_id, use_json=True, fatal=True)[job_id]["max_nodes"][
        "number"
    ]
    assert max_nodes == REDERIVED_MAX_NODES, (
        f"max_nodes should be rederived as "
        f"ROUNDUP({TASKS}, {UPDATE_TASKS_PER_NODE})={REDERIVED_MAX_NODES}, "
        f"but is {max_nodes}"
    )
