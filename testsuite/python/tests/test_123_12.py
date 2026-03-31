############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Ticket 24934.

With Flags=IGNORE_JOBS a NodeCnt= reservation over a PartitionName may be
created even when every node in the partition is busy; without IGNORE_JOBS
the same request is denied. Uses a dedicated partition where a running job
occupies all partition nodes so placement relies on IGNORE_JOBS rather than
idle nodes only. The empty Features= field is the reproduction trigger from
bug 3214, not a documented requirement.

The regression exists only on 25.11.0 through 25.11.4 (before the fix in
25.11.5). Slurm 25.05 passes without the fix because _resv_select() still
uses the older min_nodes WILL_RUN path; do not xfail releases before 25.11.
"""

import os

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
partition = f"{test_name}_part"
testuser = atf.properties["test-user"]


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(4, [("CPUs", 2), ("RealMemory", 50)])
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def part_nodes(setup):
    node_names = sorted(n for n in atf.get_nodes().keys() if n != "DEFAULT")
    four_nodes = node_names[:4]
    nodelist = ",".join(four_nodes)
    atf.run_command(
        f"scontrol create PartitionName={partition} Nodes={nodelist} "
        f"State=UP Default=NO",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield four_nodes
    atf.run_command(
        f"scontrol delete PartitionName={partition}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="function")
def reservation():
    """Delete any reservation registered during the test at teardown."""
    created = []
    yield created
    for name in created:
        atf.run_command(
            f"scontrol delete ReservationName={name}",
            user=atf.properties["slurm-user"],
            quiet=True,
        )


@pytest.fixture(scope="function")
def busy_partition(part_nodes):
    """Occupy every node in the test partition so placement needs IGNORE_JOBS."""
    job_id = atf.submit_job_sbatch(
        f"-p {partition} -N4 --wrap='sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return part_nodes


@pytest.mark.xfail(
    (25, 11) <= atf.get_version() < (25, 11, 5),
    reason="Ticket 24934: reservation create with NodeCnt and Flags=IGNORE_JOBS fails when partition nodes are occupied",
)
def test_reservation_nodecnt_ignore_jobs_busy_partition(busy_partition, reservation):
    """NodeCnt + partition + IGNORE_JOBS succeeds when jobs use all nodes."""

    resv_auto = f"{test_name}_auto"
    resv_exp = f"{test_name}_exp"

    # Baseline: an explicit Nodes= IGNORE_JOBS reservation over the busy nodes
    # must succeed first, isolating the NodeCnt path below from the IGNORE_JOBS
    # behavior itself.
    two_nodes = ",".join(busy_partition[:2])
    cmd_exp = (
        f"scontrol create reservation ReservationName={resv_exp} "
        f"PartitionName={partition} Nodes={two_nodes} StartTime=now "
        f"Duration=30 Users={testuser} Flags=IGNORE_JOBS"
    )
    out_exp = atf.run_command(
        cmd_exp,
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        out_exp["exit_code"] == 0
    ), f"Explicit Nodes= should succeed as baseline: {out_exp['stderr']}"
    atf.run_command(
        f"scontrol delete ReservationName={resv_exp}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    cmd_auto = (
        f"scontrol create reservation ReservationName={resv_auto} "
        f"PartitionName={partition} NodeCnt=2 StartTime=now Duration=30 "
        f"Users={testuser} Flags=IGNORE_JOBS Features="
    )
    reservation.append(resv_auto)
    out_auto = atf.run_command(
        cmd_auto,
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        out_auto["exit_code"] == 0
    ), f"Expected NodeCnt automatic selection to succeed: {out_auto['stderr']}"

    # The reservation must actually hold 2 of the (busy) partition nodes,
    # confirming IGNORE_JOBS selected occupied nodes rather than nothing.
    assert (
        atf.get_reservation_parameter(resv_auto, "NodeCnt") == 2
    ), "NodeCnt reservation did not reserve 2 nodes"
    reserved = set(
        atf.node_range_to_list(atf.get_reservation_parameter(resv_auto, "Nodes"))
    )
    assert reserved <= set(busy_partition), (
        f"Reserved nodes {sorted(reserved)} are not a subset of the busy "
        f"partition nodes {sorted(busy_partition)}"
    )


def test_reservation_nodecnt_without_ignore_jobs_fails_busy_partition(
    busy_partition, reservation
):
    """NodeCnt + partition + empty Features fails when all nodes busy, no IGNORE_JOBS."""

    resv_neg = f"{test_name}_neg"

    cmd = (
        f"scontrol create reservation ReservationName={resv_neg} "
        f"PartitionName={partition} NodeCnt=2 StartTime=now Duration=30 "
        f"Users={testuser} Features="
    )
    reservation.append(resv_neg)
    out = atf.run_command(
        cmd,
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    assert "nodes are busy" in out["stderr"].lower(), (
        "Expected reservation without IGNORE_JOBS to fail due to no idle "
        f"nodes in the partition: {out['stderr']}"
    )
