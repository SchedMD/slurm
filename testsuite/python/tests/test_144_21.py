############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify slurmctld recovery when a node's shared GRES topology changes.

Covers removing shard and mps entirely, shrinking the gpus backing them, and
growing or shrinking their count alone. A job whose allocation no longer maps
onto the reported topology is aborted as NODE_FAIL or requeued to PENDING
depending on JobRequeue, while an unrelated allocation on the same node keeps
running. Ticket 25525.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

node_name = ""
shard_count = 40
gpu_count = 4
shards_per_gpu = shard_count // gpu_count
mps_per_gpu = 100
mps_count = mps_per_gpu * gpu_count
gres_conf_gpus = """
Name=gpu Cores=0-5 File=/dev/tty[0-1]
Name=gpu Cores=6-11 File=/dev/tty[2-3]
"""


@pytest.fixture(scope="module", autouse=True)
def setup():
    global node_name

    atf.require_version(
        (26, 5, 5),
        "sbin/slurmctld",
        reason="Ticket 25525: shared GRES recovery fix added in 26.05.5",
    )
    for tty_num in range(gpu_count):
        atf.require_tty(tty_num)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("JobRequeue", "1")
    # CR_CORE is a base selector, mutually exclusive with the other CR_* ones,
    # so it must replace whatever the config variant set rather than join it.
    atf.require_config_parameter("SelectTypeParameters", "CR_CORE")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "shard")
    atf.require_config_parameter_includes("GresTypes", "mps")
    atf.require_config_file("gres.conf", gres_conf_gpus)
    atf.require_nodes(
        1,
        [
            ("Gres", f"gpu:{gpu_count}"),
            ("Sockets", 2),
            ("CoresPerSocket", 6),
            ("ThreadsPerCore", 1),
        ],
    )
    atf.require_slurm_running()

    nodes = atf.get_nodes(live=True)
    if not nodes:
        pytest.fail("Need at least one configured node")

    node_name = next(iter(nodes.keys()))


@pytest.fixture
def set_needed_gres(request):
    new_shard_count = request.param.get("shard_cnt", 0)
    new_mps_count = request.param.get("mps_cnt", 0)

    gres_str = f"gpu:{gpu_count}"
    if new_shard_count:
        gres_str = ",".join([gres_str, f"shard:{new_shard_count}"])
    if new_mps_count:
        gres_str = ",".join([gres_str, f"mps:{new_mps_count}"])

    atf.stop_slurm()
    atf.set_node_parameter(node_name, "Gres", gres_str)

    yield

    # set_node_parameter() restarts slurm while slurmctld is up, so clear any
    # job the test left running first.
    atf.cancel_all_jobs(fatal=True)
    atf.set_node_parameter(node_name, "Gres", f"gpu:{gpu_count}")


@pytest.fixture
def start_clean():
    atf.restart_slurmctld(clean=True)
    atf.start_slurmd(node_name)


@pytest.fixture
def restore_gres_conf():
    yield

    atf.require_config_file("gres.conf", gres_conf_gpus)


def _shrink_backing_gpus(node_name, new_gpu_count, shared_type, shared_per_gpu):
    """Reduce the GPUs backing the shards, keeping shards configured.

    gres.conf(5) warns that changing the count of File records without draining
    the node first aborts any job using those GRES. A shard job's saved bitmap
    is sized by the old backing GPU count, so it ends up larger than the shard
    topology the smaller GPU set produces.
    """

    # Keep the surviving gpus' core binding identical to gres_conf_gpus so they
    # are configured exactly as before and only the removed gpus differ.
    atf.require_config_file(
        "gres.conf", f"Name=gpu Cores=0-5 File=/dev/tty[0-{new_gpu_count - 1}]"
    )
    atf.set_node_parameter(
        node_name,
        "Gres",
        f"gpu:{new_gpu_count},{shared_type}:{new_gpu_count * shared_per_gpu}",
    )


def _slurmd_start_time(node_name):
    output = atf.run_command_output(
        f"scontrol show node {node_name}", fatal=True, quiet=True
    )
    match = re.search(r"SlurmdStartTime=(\S+)", output)
    return match.group(1) if match else None


# atf.start_slurmd() only waits for the slurmd process to exist, not for the
# node to re-register, which must happen before slurmctld is restarted.
def _restart_slurmd_and_wait(node_name):
    """Restart slurmd and wait for the node to re-register.

    Otherwise the node state slurmctld saves still carries the old shared GRES
    topology and the recovered allocation is never found to be invalid.
    """
    before = _slurmd_start_time(node_name)
    assert (
        before is not None
    ), f"Need a baseline SlurmdStartTime on {node_name} to detect re-registration"
    atf.start_slurmd(node_name)
    for _ in atf.timer(fatal=True):
        current = _slurmd_start_time(node_name)
        if current is not None and current != before:
            break


def _require_job_shared_details(job_id, shared_type, shareds_per_gpu):
    gres = str(atf.get_job(job_id, quiet=True).get("GRES", ""))
    match = re.search(shared_type + r":1\(([^)]*)\)", gres)
    if not match:
        pytest.fail(f"Expected a detailed {shared_type} allocation, got GRES={gres}")
    expected = sorted(
        [f"1/{shareds_per_gpu}"] + [f"0/{shareds_per_gpu}"] * (gpu_count - 1)
    )
    assert sorted(match.group(1).split(",")) == expected, (
        f"Expected 1 shard allocated on one of {gpu_count} GPUs carrying "
        f"{shareds_per_gpu} shards each, got GRES={gres}"
    )


def _parse_shared_detail(gres_str, shared_type):
    """Return (allocated, available) totals from a shared gres detail string."""

    match = re.search(shared_type + r":\d+\(([^)]*)\)", gres_str)
    if not match:
        return None
    allocated = available = 0
    for entry in match.group(1).split(","):
        alloc, _, avail = entry.partition("/")
        available += int(avail)
        # A gpu consumed whole by a sharing job reports "-" rather than a count.
        if alloc.isdigit():
            allocated += int(alloc)
    return allocated, available


def _require_job_shared_allocated(job_id, shared_type, expected_avail):
    """Assert the job holds one shared gres and carries the node's new total.

    The allocated counts do not move when the configured count changes, so the
    per-GPU capacities are what prove the change reached the job record.
    """

    gres = str(atf.get_job(job_id, quiet=True).get("GRES", ""))
    detail = _parse_shared_detail(gres, shared_type)
    if detail is None:
        pytest.fail(f"Expected a detailed {shared_type} allocation, got GRES={gres}")
    allocated, available = detail
    assert allocated == 1, (
        f"Expected 1 {shared_type} still allocated on exactly one of "
        f"{gpu_count} GPUs, got GRES={gres}"
    )
    assert available == expected_avail, (
        f"Expected {expected_avail} {shared_type} across the GPUs of job "
        f"{job_id}, got GRES={gres}"
    )


def _require_node_shared_avail(node_name, shared_type, expected_avail):
    """Wait for the node to report expected_avail shared gres across its GPUs.

    A count change is only absorbed as the node registers, and it moves the
    per-GPU capacities rather than the allocated counts.
    """

    detail = None
    for _ in atf.timer():
        output = atf.run_command_output(
            f"scontrol show node -d {node_name}", fatal=True, quiet=True
        )
        match = re.search(r"^\s*GresUsed=(\S*)", output, re.MULTILINE)
        detail = _parse_shared_detail(match.group(1) if match else "", shared_type)
        if detail and detail[1] == expected_avail:
            break
    if detail is None:
        pytest.fail(f"Expected a detailed {shared_type} entry on {node_name}")
    allocated, available = detail
    assert available == expected_avail, (
        f"Expected {expected_avail} {shared_type} across the GPUs on "
        f"{node_name}, got {available}"
    )
    assert (
        allocated == 1
    ), f"Expected 1 {shared_type} allocated on {node_name}, got {allocated}"


def _require_node_gres_used(node_name, expected):
    """Wait for the node to report expected in its GresUsed field.

    The shared gres detail is re-derived as the node registers with a freshly
    restarted slurmctld, so a job restored as RUNNING is no barrier against
    reading the pre-registration value.
    """

    gres_used = ""
    for _ in atf.timer():
        output = atf.run_command_output(
            f"scontrol show node -d {node_name}", fatal=True, quiet=True
        )
        match = re.search(r"^\s*GresUsed=(\S*)", output, re.MULTILINE)
        gres_used = match.group(1) if match else ""
        if expected in gres_used:
            break
    assert expected in gres_used, (
        f"Expected {expected} in the gres used on {node_name}, got "
        f"GresUsed={gres_used}"
    )


@pytest.mark.parametrize(
    ("no_requeue", "expected_state"),
    [("--no-requeue", "NODE_FAIL"), ("", "PENDING")],
    ids=["no_requeue", "requeue"],
)
@pytest.mark.parametrize(
    ("set_needed_gres", "shared_type", "shared_per_gpu"),
    [
        ({"shard_cnt": shard_count}, "shard", shards_per_gpu),
        ({"mps_cnt": mps_count}, "mps", mps_per_gpu),
    ],
    ids=["shard", "mps"],
    indirect=["set_needed_gres"],
)
def test_remove_shared(
    set_needed_gres,
    start_clean,
    shared_type,
    shared_per_gpu,
    no_requeue,
    expected_state,
):
    """Remove shared gres (shard and mps) under a running job.

    After the shared gres is dropped from the node's Gres= line and slurmctld is
    restarted, the controller must stay up and the recovered job must be
    aborted on that first restart, as NODE_FAIL for --no-requeue and requeued
    to PENDING for allowed requeue (same policy as an invalidated GPU
    allocation). A gpu job on the same node keeps a valid allocation and must
    be left running. A second restart, with the job already terminated, must at
    least bring slurmctld back with the recorded state intact.
    """

    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node_name} {no_requeue} --gres={shared_type}:1 --time=30 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    _require_job_shared_details(job_id, shared_type, shared_per_gpu)

    gpu_job_id = atf.submit_job_sbatch(
        f"-N1 -w {node_name} --gres=gpu:1 --time=30 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(gpu_job_id, "RUNNING", fatal=True)
    gpu_job_gres = str(atf.get_job(gpu_job_id, quiet=True).get("GRES", ""))
    assert "gpu:1(IDX:" in gpu_job_gres, (
        f"Expected a detailed gpu allocation for the unaffected job "
        f"{gpu_job_id}, got GRES={gpu_job_gres}"
    )

    # Stop slurm first so set_node_parameter doesn't restart the slurmd and expect it to be IDLE
    atf.stop_slurm()
    atf.set_node_parameter(node_name, "Gres", f"gpu:{gpu_count}")
    atf.start_slurmctld()
    _restart_slurmd_and_wait(node_name)
    atf.wait_for_job_state(job_id, expected_state, fatal=True)

    if expected_state == "PENDING":
        restarts = 0
        for _ in atf.timer():
            restarts = int(
                atf.get_job_parameter(job_id, "Restarts", default=0, quiet=True)
            )
            if restarts:
                break
        assert restarts >= 1, (
            f"Job {job_id} should have been requeued rather than only left "
            f"pending, got Restarts={restarts}"
        )

    # Only the shared gres went away, so the gpu allocation is still valid and
    # must survive the same revalidation that rejected the shared one.
    atf.wait_for_job_state(gpu_job_id, "RUNNING", fatal=True)
    surviving_gres = str(atf.get_job(gpu_job_id, quiet=True).get("GRES", ""))
    assert surviving_gres == gpu_job_gres, (
        f"Unaffected job {gpu_job_id} should keep GRES={gpu_job_gres}, got "
        f"GRES={surviving_gres}"
    )
    atf.cancel_jobs([gpu_job_id], fatal=True)

    # Both jobs are gone by now, so aborting must have completed the shared one
    # normally rather than leaving its gpus accounted for.
    atf.wait_for_node_state(node_name, "IDLE", fatal=True)
    _require_node_gres_used(node_name, "gpu:0(")

    followup_id = atf.submit_job_sbatch(
        f"-N1 -w {node_name} --gres=gpu:{gpu_count} --time=1 --wrap 'true'",
        fatal=True,
    )
    atf.wait_for_job_state(followup_id, "COMPLETED", fatal=True)

    # Validate the slurmctld does not crash on restart after node registration
    atf.restart_slurmctld()

    # Only RUNNING and SUSPENDED jobs are revalidated, so reloading the aborted
    # job asserts controller survival and that its state was not lost.
    atf.wait_for_job_state(job_id, expected_state, fatal=True)


@pytest.mark.parametrize(
    ("set_needed_gres", "shared_type", "shared_per_gpu"),
    [
        ({"shard_cnt": shard_count}, "shard", shards_per_gpu),
        ({"mps_cnt": mps_count}, "mps", mps_per_gpu),
    ],
    ids=["shard", "mps"],
    indirect=["set_needed_gres"],
)
def test_shrink_backing_gpus(
    set_needed_gres, start_clean, restore_gres_conf, shared_type, shared_per_gpu
):
    """Shrink the gpus backing the shared gres; the job must be aborted.

    Removing every shared gres is not the only way to invalidate a saved
    allocation. Halving the GPUs that back the shared gres, which gres.conf(5)
    warns about, also leaves the saved bitmap wider than the gpu count the node
    now reports. That width mismatch is what rejects the job here, so this
    guards the long standing gpu count comparison rather than the shared
    topology check, while still requiring slurmctld to survive rebuilding the
    shared gres details from the recovered allocation.
    """

    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node_name} --no-requeue --gres={shared_type}:1 --time=30 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    _require_job_shared_details(job_id, shared_type, shared_per_gpu)

    atf.stop_slurm()
    _shrink_backing_gpus(node_name, gpu_count // 2, shared_type, shared_per_gpu)
    atf.start_slurmctld()
    _restart_slurmd_and_wait(node_name)

    atf.wait_for_job_state(job_id, "NODE_FAIL", fatal=True)

    atf.wait_for_node_state(node_name, "IDLE", fatal=True)
    _require_node_gres_used(node_name, f"{shared_type}:0(")

    # Validate the slurmctld does not crash on restart after node registration
    atf.restart_slurmctld()

    atf.wait_for_job_state(job_id, "NODE_FAIL", fatal=True)


@pytest.mark.parametrize(
    ("increment", "expected_state"),
    [(-1, "NODE_FAIL"), (1, "RUNNING")],
    ids=["shrink", "grow"],
)
@pytest.mark.parametrize(
    ("set_needed_gres", "shared_type", "shared_cnt", "shared_per_gpu"),
    [
        ({"shard_cnt": shard_count}, "shard", shard_count, shards_per_gpu),
        ({"mps_cnt": mps_count}, "mps", mps_count, mps_per_gpu),
    ],
    ids=["shard", "mps"],
    indirect=["set_needed_gres"],
)
def test_increment_shared(
    set_needed_gres,
    start_clean,
    shared_type,
    shared_cnt,
    shared_per_gpu,
    increment,
    expected_state,
):
    """Change only the shared gres count under a running job.

    Shrinking it by one leaves the job's saved allocation covering more shared
    gres than the node is now configured for, so the job must be aborted on the
    first restart, while topo_* still carries the pre-change totals it only
    refreshes at node registration. Growing it by one takes nothing away from
    the job, so it must stay running with its allocation intact and is the
    control against rejecting more than the shrink warrants.
    """

    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node_name} --no-requeue --gres={shared_type}:1 --time=30 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    _require_job_shared_details(job_id, shared_type, shared_per_gpu)

    # Stop slurm first so set_node_parameter doesn't restart the slurmd and
    # expect it to be IDLE
    atf.stop_slurm()
    atf.set_node_parameter(
        node_name, "Gres", f"gpu:{gpu_count},{shared_type}:{shared_cnt + increment}"
    )
    atf.start_slurmctld()
    _restart_slurmd_and_wait(node_name)
    atf.wait_for_job_state(job_id, expected_state, fatal=True)

    if expected_state == "RUNNING":
        # The new capacity is unreachable until the node registers, so this is
        # the first point the count change can be proven to have landed.
        _require_node_shared_avail(node_name, shared_type, shared_cnt + increment)

    # Validate the slurmctld does not crash on restart after node registration
    atf.restart_slurmctld()

    # A job that outlives the change is only really unaffected if it comes back
    # from job_state with its allocation intact, not merely still RUNNING.
    atf.wait_for_job_state(job_id, expected_state, fatal=True)
    if expected_state == "RUNNING":
        # The job's gres detail is rebuilt from node state during recovery, so
        # this restart is where the new capacity reaches the job record.
        _require_job_shared_allocated(job_id, shared_type, shared_cnt + increment)
        _require_node_shared_avail(node_name, shared_type, shared_cnt + increment)
    else:
        atf.wait_for_node_state(node_name, "IDLE", fatal=True)
        _require_node_gres_used(node_name, f"{shared_type}:0(")
