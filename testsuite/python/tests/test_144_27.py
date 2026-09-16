############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify --gres-flags=enforce-links alongside --gres-flags=enforce-binding.

enforce-binding ties a job's GRES to the sockets it holds cores on;
enforce-links requires every device it gets to be linked to the others. Both
apply, so the linked set has to come from the sockets its cores landed on. A
search ranging over the whole node would satisfy the links and quietly break
the binding.

Three nodes, two sockets. node1 and node2 have four GPUs on socket 0 and two
on socket 1:
  - node1: {0,1,2,3} all linked, and {4,5} linked. Either socket can meet a
    two-GPU request, but socket 1's set is the smaller, and selection prefers
    the smallest that fits, so it is drawn there whatever socket the job holds.
    That is what catches an unbound search.
  - node2: only {4,5} are linked, so a two-GPU request can only be met on
    socket 1.

node3 has three GPUs on each socket, with one linked pair per socket: {0,1}
on socket 0 and {3,4} on socket 1. With one socket short of cores, the other
socket's pair must still be usable.

Each case is paired with a counter-example holding one flag on its own.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

SOCKETS = 2
CORES_PER_SOCKET = 4
GPUS = 6

# Socket 0 holds cores 0-3 and GPUs 0-3; socket 1 holds cores 4-7 and GPUs 4,5.
CORES_ON_SOCKET = {
    0: set(range(0, CORES_PER_SOCKET)),
    1: set(range(CORES_PER_SOCKET, SOCKETS * CORES_PER_SOCKET)),
}
GPUS_ON_SOCKET = {0: {0, 1, 2, 3}, 1: {4, 5}}
# node3: GPUs 0-2 on socket 0 and 3-5 on socket 1, one linked pair on each.
NODE3_PAIR_ON_SOCKET = {0: {0, 1}, 1: {3, 4}}


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="Issue 51169: --gres-flags=enforce-links added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Issue 51169: --gres-flags=enforce-links added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Overwrite rather than add to what a config variant set: adding CR_CORE to
    # a variant that already selected another CR_* type is rejected by Slurm.
    atf.require_config_parameter("SelectTypeParameters", "CR_CORE")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(
        3,
        [
            ("Gres", f"gpu:{GPUS}"),
            ("Sockets", SOCKETS),
            ("CoresPerSocket", CORES_PER_SOCKET),
            ("ThreadsPerCore", 1),
        ],
    )

    gpu_file = f"{atf.module_tmp_path}/gpu"
    for i in range(GPUS):
        atf.run_command(f"touch {gpu_file}{i}", fatal=True)

    # node1: socket 0 holds a linked set of four, socket 1 a linked set of two.
    # node2: socket 0's GPUs link to nothing, socket 1 holds a linked pair.
    # node3: each socket holds a linked pair and a GPU that links to nothing.
    atf.require_config_parameter(
        "NodeName",
        f"""node1 Name=gpu File={gpu_file}0 Cores=0-3 Links=-1,1,1,1,0,0
        NodeName=node1 Name=gpu File={gpu_file}1 Cores=0-3 Links=1,-1,1,1,0,0
        NodeName=node1 Name=gpu File={gpu_file}2 Cores=0-3 Links=1,1,-1,1,0,0
        NodeName=node1 Name=gpu File={gpu_file}3 Cores=0-3 Links=1,1,1,-1,0,0
        NodeName=node1 Name=gpu File={gpu_file}4 Cores=4-7 Links=0,0,0,0,-1,1
        NodeName=node1 Name=gpu File={gpu_file}5 Cores=4-7 Links=0,0,0,0,1,-1
        NodeName=node2 Name=gpu File={gpu_file}0 Cores=0-3 Links=-1,0,0,0,0,0
        NodeName=node2 Name=gpu File={gpu_file}1 Cores=0-3 Links=0,-1,0,0,0,0
        NodeName=node2 Name=gpu File={gpu_file}2 Cores=0-3 Links=0,0,-1,0,0,0
        NodeName=node2 Name=gpu File={gpu_file}3 Cores=0-3 Links=0,0,0,-1,0,0
        NodeName=node2 Name=gpu File={gpu_file}4 Cores=4-7 Links=0,0,0,0,-1,1
        NodeName=node2 Name=gpu File={gpu_file}5 Cores=4-7 Links=0,0,0,0,1,-1
        NodeName=node3 Name=gpu File={gpu_file}0 Cores=0-3 Links=-1,1,0,0,0,0
        NodeName=node3 Name=gpu File={gpu_file}1 Cores=0-3 Links=1,-1,0,0,0,0
        NodeName=node3 Name=gpu File={gpu_file}2 Cores=0-3 Links=0,0,-1,0,0,0
        NodeName=node3 Name=gpu File={gpu_file}3 Cores=4-7 Links=0,0,0,-1,1,0
        NodeName=node3 Name=gpu File={gpu_file}4 Cores=4-7 Links=0,0,0,1,-1,0
        NodeName=node3 Name=gpu File={gpu_file}5 Cores=4-7 Links=0,0,0,0,0,-1""",
        source="gres",
    )
    atf.require_slurm_running()


def _parse_idx(idx_str):
    """Expand an 'IDX' string like '0,2-3' into a set of ints."""
    indexes = set()
    for part in idx_str.split(","):
        if "-" in part:
            start, end = part.split("-")
            indexes.update(range(int(start), int(end) + 1))
        else:
            indexes.add(int(part))
    return indexes


def _job_gpus_and_cpus(job_id):
    """Return (set of GPU indexes, set of CPU ids) allocated to a job."""
    out = atf.run_command_output(f"scontrol show job {job_id} -d", fatal=True)
    gpus = re.search(r"GRES=gpu:[^(]*\(IDX:([0-9,\-]+)\)", out)
    cpus = re.search(r"CPU_IDs=([0-9,\-]+)", out)
    assert gpus is not None, f"No GPU IDX for job {job_id}:\n{out}"
    assert cpus is not None, f"No CPU_IDs for job {job_id}:\n{out}"
    return _parse_idx(gpus.group(1)), _parse_idx(cpus.group(1))


def _sockets_of(indexes, by_socket):
    """Return the set of sockets the given core or GPU indexes lie on."""
    return {s for s, members in by_socket.items() if indexes & members}


def _assert_gpus_are_bound(gpus, cpus):
    """Every GPU must lie on a socket the job holds at least one core on."""
    gpu_sockets = _sockets_of(gpus, GPUS_ON_SOCKET)
    cpu_sockets = _sockets_of(cpus, CORES_ON_SOCKET)
    assert gpu_sockets <= cpu_sockets, (
        f"enforce-binding violated: GPUs {sorted(gpus)} lie on sockets "
        f"{sorted(gpu_sockets)}, but the job holds cores only on sockets "
        f"{sorted(cpu_sockets)} (CPUs {sorted(cpus)})"
    )


def _assert_mutually_linked(gpus, linked_sets):
    """The GPUs must all come from one of the node's linked sets."""
    assert any(gpus <= s for s in linked_sets), (
        f"enforce-links violated: GPUs {sorted(gpus)} are not all drawn from "
        f"one linked set ({[sorted(s) for s in linked_sets]})"
    )


def test_enforce_binding_links_keeps_the_linked_set_on_a_held_socket():
    """Selection prefers the smallest set that fits, socket 1's pair, so an
    unbound search returns it whatever socket the job holds. Whichever socket
    the job lands on, its GPUs must be on one it holds a core of."""
    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-binding,enforce-links --gpus-per-node=2 "
        '-n1 -c1 -w node1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), "the job should run on node1, whose every socket holds a linked set"

    gpus, cpus = _job_gpus_and_cpus(job_id)
    _assert_gpus_are_bound(gpus, cpus)
    _assert_mutually_linked(gpus, [{0, 1, 2, 3}, {4, 5}])


def test_enforce_binding_links_on_a_node_bound_to_one_socket():
    """node2's only pair is {4,5} on socket 1, so the job must get those and a
    core of socket 1. Socket 0 holds the GPUs and cores it would otherwise be
    given first, so either flag alone would produce a different answer."""
    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-binding,enforce-links --gpus-per-node=2 "
        '-n1 -c1 -w node2 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), "the job should run on node2, whose socket 1 holds a linked pair"

    gpus, cpus = _job_gpus_and_cpus(job_id)
    assert gpus == {4, 5}, f"Expected the linked pair {{4, 5}}, got {sorted(gpus)}"
    _assert_gpus_are_bound(gpus, cpus)
    assert (
        cpus <= CORES_ON_SOCKET[1]
    ), f"the job should hold cores on socket 1 alongside its GPUs, got {sorted(cpus)}"


def test_enforce_binding_alone_ignores_the_links_on_node2():
    """Counter-example: without enforce-links the job gets socket 0's unlinked
    GPUs, so the pair above is the links and not the binding alone."""
    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-binding --gpus-per-node=2 "
        '-n1 -c1 -w node2 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), "a job without enforce-links should run on node2"

    gpus, cpus = _job_gpus_and_cpus(job_id)
    _assert_gpus_are_bound(gpus, cpus)
    assert gpus <= GPUS_ON_SOCKET[0], (
        f"without the links the job should take socket 0's GPUs, " f"got {sorted(gpus)}"
    )


def test_enforce_links_alone_need_not_bind_on_node2():
    """Counter-example: without enforce-binding the job still gets the linked
    pair, so the binding is what ties its cores to socket 1."""
    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-links --gpus-per-node=2 "
        '-n1 -c1 -w node2 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), "a job without enforce-binding should run on node2"

    gpus, _ = _job_gpus_and_cpus(job_id)
    assert gpus == {4, 5}, f"Expected the linked pair {{4, 5}}, got {sorted(gpus)}"


def test_enforce_binding_links_per_task_keeps_the_linked_set_on_a_held_socket():
    """A per-task request reaches selection by a different path
    (_set_task_bits), so cover it on the same node."""
    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-binding,enforce-links --gpus-per-task=2 "
        '-n1 -c1 -w node1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), "the job should run on node1, whose every socket holds a linked set"

    gpus, cpus = _job_gpus_and_cpus(job_id)
    _assert_gpus_are_bound(gpus, cpus)
    _assert_mutually_linked(gpus, [{0, 1, 2, 3}, {4, 5}])


def test_enforce_binding_links_uses_the_pair_on_a_socket_with_cores():
    """A job holding three of one socket's four cores leaves it one core, too
    few for --cpus-per-gpu=2, so its pair cannot be bound. The other socket
    still has every core and its own linked pair, so a job with both flags
    must run there rather than wait for the first socket to free up."""
    blocker_id = atf.submit_job_sbatch(
        '-n3 --ntasks-per-socket=3 -w node3 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        blocker_id, "RUNNING", fatal=True
    ), "the job holding three cores should run on node3"
    out = atf.run_command_output(f"scontrol show job {blocker_id} -d", fatal=True)
    match = re.search(r"CPU_IDs=([0-9,\-]+)", out)
    assert match is not None, f"No CPU_IDs for job {blocker_id}:\n{out}"
    held_sockets = _sockets_of(_parse_idx(match.group(1)), CORES_ON_SOCKET)
    assert (
        len(held_sockets) == 1
    ), f"--ntasks-per-socket=3 should keep the job on one socket, got {held_sockets}"
    free_socket = 1 - held_sockets.pop()

    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-binding,enforce-links --gpus=2 --cpus-per-gpu=2 "
        '-N1 -n1 -w node3 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), f"the job should run on socket {free_socket}, which has its cores and a linked pair"

    gpus, cpus = _job_gpus_and_cpus(job_id)
    assert gpus == NODE3_PAIR_ON_SOCKET[free_socket], (
        f"Expected socket {free_socket}'s pair "
        f"{sorted(NODE3_PAIR_ON_SOCKET[free_socket])}, got {sorted(gpus)}"
    )
    assert cpus <= CORES_ON_SOCKET[free_socket], (
        f"the job should hold cores on socket {free_socket} alongside its "
        f"GPUs, got {sorted(cpus)}"
    )
