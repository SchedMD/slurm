############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test jobs requesting multiple reservations, and multiple partitions.

A job listing an INACTIVE reservation alongside an ACTIVE one must start in the
ACTIVE one, regardless of list order, partition list, or EnforcePartLimits.
"""

import pytest

import atf

resv_inactive = "resv_inactive"
resv_active = "resv_active"
part_all = "part_all"
part_low = "part_low"
part_med = "part_med"
node_low = None
node_med = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global node_low, node_med

    atf.require_nodes(2)
    node_low, node_med = list(atf.get_nodes(live=False))[-2:]
    atf.require_config_parameter(
        "PartitionName",
        {
            part_all: {"Nodes": f"{node_low},{node_med}", "Default": "YES"},
            part_low: {"Nodes": node_low},
            part_med: {"Nodes": node_med},
        },
    )
    atf.require_slurm_running()


@pytest.fixture(scope="module", params=["NO", "ANY", "ALL"])
def enforce_part_limits(request):
    atf.set_config_parameter("EnforcePartLimits", request.param)


# Allocation at submit tries a job's reservations in reservation id order, so
# test with the INACTIVE reservation both older and newer than the ACTIVE one.
@pytest.fixture(scope="module", params=["inactive_older", "active_older"])
def reservations(request):
    resvs = [
        (resv_inactive, "now+1day", node_low),
        (resv_active, "now", node_med),
    ]
    if request.param == "active_older":
        resvs.reverse()

    for name, start, node in resvs:
        atf.run_command(
            f"scontrol create reservation reservationname={name} "
            f"start={start} duration=1:00:00 nodes={node} "
            f"user={atf.properties['test-user']}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    yield

    for name, _, _ in resvs:
        atf.run_command(
            f"scontrol delete reservation {name}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )


@pytest.mark.parametrize("command", ["sbatch", "salloc"])
@pytest.mark.parametrize(
    "resv_list",
    [f"{resv_inactive},{resv_active}", f"{resv_active},{resv_inactive}"],
)
@pytest.mark.parametrize(
    "part_list, part_used",
    [(None, part_all), (f"{part_low},{part_med}", part_med), (part_med, part_med)],
)
def test_start_in_active_resv(
    enforce_part_limits, reservations, command, resv_list, part_list, part_used
):
    """Test that the job starts in the ACTIVE reservation"""

    part_arg = f"-p {part_list}" if part_list else ""
    job_id = atf.submit_job(
        command,
        f"{part_arg} --reservation={resv_list} -t1",
        "sleep infinity",
        background=True,
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    node = atf.get_job_parameter(job_id, "NodeList", fatal=True)
    assert node == node_med, f"Job should run on {node_med}, not {node}"
    resv = atf.get_job_parameter(job_id, "Reservation", fatal=True)
    assert resv == resv_list, f"Job should list {resv_list}, not {resv}"
    part = atf.get_job_parameter(job_id, "Partition", fatal=True)
    assert part == part_used, f"Job should run in {part_used}, not {part}"


@pytest.mark.parametrize("resv_list", ["resv_bogus,{active}", "{active},resv_bogus"])
def test_invalid_resv_rejected(reservations, resv_list):
    """Test that a job listing an unknown reservation is rejected"""

    resv_list = resv_list.format(active=resv_active)
    result = atf.run_command(
        f"sbatch --reservation={resv_list} --wrap true",
        xfail=True,
        fatal=True,
    )
    assert (
        "Requested reservation is invalid" in result["stderr"]
    ), f"sbatch should reject {resv_list} as an invalid reservation"
