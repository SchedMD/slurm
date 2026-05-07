############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Tickets 24026/24080: reservation access to other reservations' resources.

Covers both directions: which nodes and cores held by another reservation
a FLEX/OVERLAP reservation must not take, and which ones OVERLAP and MAINT
are documented to grant it.
"""

import re

import pytest

import atf

LICENSE = "testlic"
skip_if_linear = pytest.mark.skipif(
    atf.get_config_parameter("SelectType", default="", live=False) == "select/linear",
    reason="Ticket 24080: partial-node reservations require cons_tres",
)
requires_ticket_24026 = pytest.mark.skipif(
    atf.get_version() < (25, 11, 4),
    reason="Ticket 24026: FLEX reservations steal fix landed in 25.11.4",
)
requires_ticket_24080 = pytest.mark.skipif(
    atf.get_version() < (26, 5, 5),
    reason="Ticket 24080: OVERLAP+FLEX reservations steal fix landed in 26.05.5",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(4, [("CPUs", 4)])
    atf.require_config_parameter("Licenses", f"{LICENSE}:10")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def nodes():
    return atf.run_job_nodes("-N4 true", fatal=True)


@pytest.fixture(scope="module")
def exclude_arg(nodes):
    """--exclude argument keeping FLEX jobs inside the four test nodes.

    FLEX reservations make the job eligible for every node in the
    partition, so on clusters with more than four nodes the jobs would
    float away from the nodes the assertions reason about.
    """
    other_nodes = set(atf.get_nodes(quiet=True)) - set(nodes)
    if not other_nodes:
        return ""
    return f" --exclude={atf.node_list_to_range(sorted(other_nodes))}"


@pytest.fixture(autouse=True)
def cleanup_resvs():
    yield
    # A reservation cannot be deleted while jobs are still running in it,
    # and the conftest job cleanup only runs after this fixture.
    atf.cancel_all_jobs(fatal=True)
    for name in ("res1", "res2"):
        atf.run_command(
            f"scontrol delete reservationname={name}",
            user=atf.properties["slurm-user"],
            quiet=True,
        )


def assert_node_off_limits(sbatch_args, node):
    """Asserts a job cannot obtain node, either outcome the docs allow.

    The documentation promises only that the node is off limits, not how the
    request is refused, so both a submission rejection and an accepted job
    that never gets the node are conforming.
    """
    result = atf.run_command(
        f"sbatch {sbatch_args} --wrap='sleep infinity'", xfail=True
    )

    if result["exit_code"] != 0:
        assert (
            "outside of the reservation" in result["stderr"]
        ), f"submission was refused for an unrelated reason: {result['stderr']}"
        return

    job = int(re.search(r"Submitted \S+ job (\d+)", result["stdout"]).group(1))
    atf.properties["submitted-jobs"].append(job)
    assert atf.wait_for_job_state(
        job, "PENDING", desired_reason="Resources"
    ), f"job {job} was allowed to take {node}"


def create_resv(name, nodelist=None, flags=None, tres=None, licenses=None):
    user = atf.properties["test-user"]
    extra = ""
    if nodelist:
        extra += f" nodes={nodelist}"
    if flags:
        extra += f" flags={flags}"
    if tres:
        extra += f" tres={tres}"
    if licenses:
        extra += f" licenses={licenses}"
    atf.run_command(
        f"scontrol create reservation reservationname={name} "
        f"starttime=now duration=infinite user={user}{extra}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@requires_ticket_24080
def test_overlap_flex_does_not_steal_non_overlapping(nodes, exclude_arg):
    """Ticket 24080: OVERLAP+FLEX must not steal from a non-overlapping resv.

    res1 (FLEX,OVERLAP) does not share any node with res2, so jobs in res1
    must never land on the node res2 reserves.
    """
    n1, n2, n3, *_ = nodes
    create_resv("res1", f"{n1},{n2}", flags="FLEX,OVERLAP")
    create_resv("res2", n3, flags="FLEX")

    jobs = [
        atf.submit_job_sbatch(
            f"--reservation=res1 --exclusive -N1{exclude_arg} "
            "--wrap='sleep infinity'",
            fatal=True,
        )
        for _ in range(4)
    ]

    # A controller that steals passes through three running jobs on its way to
    # four, so only evidence of a steal ends the wait early. Otherwise the whole
    # window is watched before the settled state is asserted.
    running_nodes = set()
    for _ in atf.timer():
        job_info = atf.get_jobs(quiet=True)
        running_nodes = {
            job_info[j]["NodeList"]
            for j in jobs
            if job_info[j]["JobState"] == "RUNNING"
        }
        if n3 in running_nodes or len(running_nodes) > 3:
            break

    assert n3 not in running_nodes, f"a job stole {n3} reserved by res2"
    assert (
        len(running_nodes) == 3
    ), f"expected 3 running jobs avoiding {n3}, got {running_nodes}"


@skip_if_linear
@pytest.mark.parametrize(
    "flags",
    [
        pytest.param("FLEX", marks=requires_ticket_24026),
        pytest.param("FLEX,OVERLAP", marks=requires_ticket_24080),
    ],
)
def test_does_not_steal_partial_node(nodes, flags):
    """Tickets 24026/24080: res1 must not steal cores from a partial-node resv.

    Reservation res2 reserves just 1 CPU. The job asks for every CPU on
    the node, so it must stay pending on resources.
    """
    n1, n2, *_ = nodes
    cpus = int(atf.get_node_parameter(n2, "cpus"))
    create_resv("res1", n1, flags=flags)
    create_resv("res2", n2, tres="cpu=1")

    job = atf.submit_job_sbatch(
        f"--reservation=res1 -w {n2} -c{cpus} --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        job, "PENDING", desired_reason="Resources"
    ), f"job {job} should pend on Resources: res2 reserves a core on {n2}"

    # Only res2's cores are off limits, not the whole node, so a job that
    # leaves them free must still run there. The pending job is cancelled
    # first so it cannot hold the second one back in the queue.
    atf.cancel_jobs([job], fatal=True)
    job = atf.submit_job_sbatch(
        f"--reservation=res1 -w {n2} -c{cpus - 1} --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job, "RUNNING"
    ), f"job {job} should run on {n2}: res2 reserves cores there, not the node"


@skip_if_linear
def test_partial_node_maint_excludes_cores_not_node(nodes):
    """A partial-node MAINT reservation excludes its cores, not its node.

    res2 holds a single core on n2 under MAINT, so a job in res1 leaving
    that core free must still run there.
    """
    n1, n2, *_ = nodes
    cpus = int(atf.get_node_parameter(n2, "cpus"))
    create_resv("res2", n2, flags="MAINT", tres="cpu=1")
    create_resv("res1", n1, flags="FLEX")

    job = atf.submit_job_sbatch(
        f"--reservation=res1 -w {n2} -c{cpus - 1} --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job, "RUNNING"
    ), f"job {job} should run on {n2}: res2 reserves a core there, not the node"


def test_flex_does_not_steal_full_node(nodes):
    """FLEX vs full-node res2 must not steal nodes.

    res2 reserves the whole node, so a job in res1 (FLEX) explicitly
    requesting that node must never be allowed to run there.
    """
    n1, n2, *_ = nodes
    create_resv("res1", n1, flags="FLEX")
    create_resv("res2", n2)

    assert_node_off_limits(f"--reservation=res1 -w {n2} --exclusive", n2)


def test_overlap_flex_preserves_shared_node_access(nodes):
    """OVERLAP+FLEX must keep access to nodes shared with res2.

    res1 (FLEX,OVERLAP) and res2 both contain n2. OVERLAP is documented to
    allow res1 to be allocated resources that are already in another
    reservation, so a job in res1 explicitly requesting the shared node
    must still run on it.
    """
    n1, n2, *_ = nodes
    create_resv("res1", f"{n1},{n2}", flags="FLEX,OVERLAP")
    create_resv("res2", n2)

    job = atf.submit_job_sbatch(
        f"--reservation=res1 -w {n2} --exclusive --wrap='sleep infinity'"
    )
    assert job, f"job requesting shared node {n2} was rejected at submission"
    ran = atf.wait_for_job_state(job, "RUNNING")
    assert ran, f"job {job} could not run on {n2}, a node shared by res1 and res2"
    assert (
        atf.get_job_parameter(job, "NodeList") == n2
    ), f"job {job} did not run on the requested shared node {n2}"


@requires_ticket_24080
def test_overlap_flex_partially_overlapping_res2(nodes):
    """Ticket 24080: OVERLAP+FLEX keeps shared nodes but not unshared ones.

    res1 (FLEX,OVERLAP) holds n1 and n2 while res2 holds n2 and n3, so n2
    belongs to both and n3 belongs only to res2. OVERLAP still grants the
    shared node, and only the unshared one is removed.
    """
    n1, n2, n3, *_ = nodes
    create_resv("res1", f"{n1},{n2}", flags="FLEX,OVERLAP")
    create_resv("res2", f"{n2},{n3}")

    # n3 is only in res2, so res1 must not steal it.
    assert_node_off_limits(f"--reservation=res1 -w {n3} --exclusive", n3)

    # n2 belongs to both reservations, so OVERLAP still grants it; only the
    # nodes held solely by res2 are excluded.
    shared_job = atf.submit_job_sbatch(
        f"--reservation=res1 -w {n2} --exclusive --wrap='sleep infinity'"
    )
    assert shared_job, f"job requesting shared node {n2} was rejected at submission"
    assert atf.wait_for_job_state(
        shared_job, "RUNNING"
    ), f"job {shared_job} could not run on {n2}, a node shared by res1 and res2"
    assert (
        atf.get_job_parameter(shared_job, "NodeList") == n2
    ), f"job {shared_job} did not run on the requested shared node {n2}"


def test_pure_overlap_preserves_shared_node_access(nodes):
    """OVERLAP (no FLEX) must keep access to a node shared with res2.

    OVERLAP is documented to allow res1 to be allocated resources that are
    already in another reservation, so all four jobs must run, one of them
    on the shared node.
    """
    n1, n2, n3, n4 = nodes
    create_resv("res1", f"{n1},{n2},{n3},{n4}", flags="OVERLAP")
    create_resv("res2", n2)

    jobs = [
        atf.submit_job_sbatch(
            "--reservation=res1 --exclusive -N1 --wrap='sleep infinity'", fatal=True
        )
        for _ in range(4)
    ]
    running_nodes = set()
    for _ in atf.timer():
        job_info = atf.get_jobs(quiet=True)
        states = [job_info[j]["JobState"] for j in jobs]
        running_nodes = {
            job_info[j]["NodeList"]
            for j in jobs
            if job_info[j]["JobState"] == "RUNNING"
        }
        if states.count("RUNNING") == 4:
            break

    assert (
        n2 in running_nodes
    ), f"OVERLAP semantics broken: job in res1 could not use shared node {n2}"
    assert (
        len(running_nodes) == 4
    ), f"expected all 4 jobs running on {{n1..n4}}, got {running_nodes}"


def test_flex_without_overlap_loses_shared_node(nodes):
    """Ticket 24080: sharing a node is not enough without OVERLAP.

    n2 belongs to both reservations, but res1 has no OVERLAP flag, so the
    node res2 also holds stays off limits to res1's jobs.
    """
    n1, n2, *_ = nodes
    create_resv("res1", f"{n1},{n2}", flags="FLEX")
    create_resv("res2", n2, flags="OVERLAP")

    assert_node_off_limits(f"--reservation=res1 -w {n2} --exclusive", n2)


def test_nodeless_overlap_flex_keeps_full_grant(nodes):
    """Ticket 24080: a nodeless OVERLAP+FLEX resv keeps the full grant.

    res1 holds only a license, so it reserves no nodes of its own and
    keeps access to every node, including the one res2 reserves.
    """
    n1, n2, *_ = nodes
    create_resv("res2", n2)
    create_resv("res1", flags="ANY_NODES,FLEX,OVERLAP", licenses=f"{LICENSE}:1")

    job = atf.submit_job_sbatch(
        f"--reservation=res1 --licenses={LICENSE}:1 -w {n2} --exclusive "
        "--wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job, "RUNNING"
    ), f"job {job} could not use {n2}: a nodeless OVERLAP resv keeps all nodes"


@pytest.mark.parametrize("flags", ["OVERLAP", "FLEX,OVERLAP"])
def test_overlap_does_not_steal_from_maint_resv(nodes, flags):
    """Ticket 24080: OVERLAP never grants access to a MAINT reservation.

    n2 belongs to both reservations, so OVERLAP would normally grant it,
    but res2 holds it under MAINT and it stays excluded either way.
    """
    n1, n2, *_ = nodes
    create_resv("res2", n2, flags="MAINT")
    create_resv("res1", f"{n1},{n2}", flags=flags)

    assert_node_off_limits(f"--reservation=res1 -w {n2} --exclusive", n2)


@requires_ticket_24080
def test_nodeless_overlap_flex_still_excludes_maint(nodes):
    """A nodeless OVERLAP reservation still cannot reach a MAINT reservation.

    res1 holds only a license, so it keeps access to every node except the
    one res2 holds under MAINT.
    """
    n1, n2, *_ = nodes
    create_resv("res2", n2, flags="MAINT")
    create_resv("res1", flags="ANY_NODES,FLEX,OVERLAP", licenses=f"{LICENSE}:1")

    assert_node_off_limits(
        f"--reservation=res1 --licenses={LICENSE}:1 -w {n2} --exclusive", n2
    )


def test_maint_bypasses_overlap_guard(nodes):
    """MAINT reservations may use nodes that other reservations claim."""
    n1, n2, *_ = nodes
    create_resv("res1", f"{n1},{n2}", flags="MAINT,IGNORE_JOBS")
    create_resv("res2", n2)

    job = atf.submit_job_sbatch(
        f"--reservation=res1 -w {n2} --exclusive --wrap='sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job, "RUNNING", fatal=True)
    assert (
        atf.get_job_parameter(job, "NodeList") == n2
    ), f"job {job} did not run on {n2} despite MAINT+IGNORE_JOBS on res1"
