############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GRES device chunks with gres.conf Cores= and --gres-flags=enforce-binding.

Each node has two sockets of eight cores and eight devices, four bound to the
cores of each socket. Under enforce-binding a job only gets devices bound to
the sockets it has cores on, so the chunk has to be one whose sockets the job
can also get cores on.
"""

import re

import pytest

import atf

GPUS = 8
CORES_PER_SOCKET = 8
SIZES = "1,2,4"
SOCKET_CPUS = [set(range(CORES_PER_SOCKET)), set(range(CORES_PER_SOCKET, 16))]
# "bound": chunk positions in device order, so every chunk below the whole
# domain lies on one socket. "span": ChunkOrder puts a device of each socket
# in every pair, so every pair spans both sockets.
SPAN_ORDER = "0,4,1,5,2,6,3,7"

node = {}


@pytest.fixture(scope="module", autouse=True)
def setup():
    # slurmd parses ChunkSizes/ChunkOrder
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="Issue 51025: gres.conf ChunkSizes/ChunkOrder are new in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(
        2,
        [
            ("CPUs", 2 * CORES_PER_SOCKET),
            ("Sockets", 2),
            ("CoresPerSocket", CORES_PER_SOCKET),
            ("ThreadsPerCore", 1),
            ("Gres", f"gpu:chunked:{GPUS}"),
        ],
    )
    for i in range(GPUS):
        atf.require_tty(i)

    gpu_nodes = sorted(
        name
        for name, params in atf.get_nodes(live=False, quiet=True, fatal=True).items()
        if name != "DEFAULT" and f"gpu:chunked:{GPUS}" in str(params.get("Gres", ""))
    )
    assert len(gpu_nodes) >= 2, f"Expected two GPU nodes, got {gpu_nodes}"
    node.update(zip(["bound", "span"], gpu_nodes))

    lines = []
    for role, extra in (("bound", ""), ("span", f" ChunkOrder={SPAN_ORDER}")):
        lines += [
            f"{node[role]} Name=gpu Type=chunked File=/dev/tty[0-3] "
            f"Cores=0-{CORES_PER_SOCKET - 1} ChunkSizes={SIZES}{extra}",
            f"{node[role]} Name=gpu Type=chunked File=/dev/tty[4-7] "
            f"Cores={CORES_PER_SOCKET}-{2 * CORES_PER_SOCKET - 1} "
            f"ChunkSizes={SIZES}{extra}",
        ]
    atf.require_config_parameter("NodeName", "\nNodeName=".join(lines), source="gres")
    atf.require_slurm_running()


def _run(job_args):
    """Submit a job, wait for it to run, and return its ID."""

    job_id = atf.submit_job_sbatch(f"{job_args} --wrap 'sleep infinity'", fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job {job_id} ({job_args}) should reach RUNNING"
    return job_id


def _job_cpus(job_id):
    output = atf.run_command_output(
        f"scontrol show job -d {job_id}", fatal=True, quiet=True
    )
    match = re.search(r"CPU_IDs=([\d,\-]+)", output)
    assert match, f"Expected CPU_IDs for job {job_id} in: {output}"
    return set(atf.range_to_list(match.group(1)))


def test_chunk_is_picked_where_the_job_can_get_cores():
    """The chunk comes from a socket that can hold it, not just a free device.

    Devices 5 and 7 are free on socket 1, but they are no aligned pair, so
    only socket 0 can hold a chunk of 2. Socket 1 has more free cores, which
    must not draw the job's cores there: its tasks could then not get a
    device of the chunk on their own socket.
    """

    name = node["bound"]
    binding = "--gres-flags=enforce-binding"
    socket0 = _run(f"-N1 -w {name} --gres=gpu:chunked:4 {binding}")
    assert atf.get_job_gres_indexes(socket0) == {
        0,
        1,
        2,
        3,
    }, "Expected the first chunk of 4 on socket 0"
    holders = {}
    for _ in range(4):
        job_id = _run(f"-N1 -w {name} --gres=gpu:chunked:1 {binding}")
        holders[next(iter(atf.get_job_gres_indexes(job_id)))] = job_id
    assert set(holders) == {
        4,
        5,
        6,
        7,
    }, f"Expected one job on each device of socket 1, got {holders}"
    atf.cancel_jobs([socket0, holders[5], holders[7]], fatal=True)

    # Takes devices 0-1 and six of socket 0's cores, so socket 0 has two free
    # cores to socket 1's six
    job_id = _run(f"-N1 -w {name} -n6 --gres=gpu:chunked:2 {binding}")
    assert atf.get_job_gres_indexes(job_id) == {0, 1}, "Expected devices 0-1"

    job_id = _run(f"-N1 -w {name} -n2 --gpus-per-task=chunked:1 {binding}")
    indexes = atf.get_job_gres_indexes(job_id)
    assert indexes == {2, 3}, f"Expected the free pair 2-3, got {indexes}"
    cpus = _job_cpus(job_id)
    assert cpus <= SOCKET_CPUS[0], f"Expected only socket 0 CPUs, got {cpus}"


def test_chunk_spanning_sockets_gets_cores_on_each():
    """A chunk whose devices are on two sockets gets the job cores on both."""

    job_id = _run(
        f"-N1 -w {node['span']} -n2 --gres=gpu:chunked:2 --gres-flags=enforce-binding"
    )
    indexes = atf.get_job_gres_indexes(job_id)
    assert indexes == {0, 4}, f"Expected positions 0-1, devices 0 and 4, got {indexes}"
    cpus = _job_cpus(job_id)
    assert all(
        cpus & socket for socket in SOCKET_CPUS
    ), f"Expected CPUs on both sockets, got {cpus}"
