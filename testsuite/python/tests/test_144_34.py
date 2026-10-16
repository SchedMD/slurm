############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GRES device chunks above size 2: nesting, best fit and ChunkOrder tiles.

Two nodes with eight devices and ChunkSizes=2,4,8. "nest" numbers its chunk
positions in device order, so every pair lies in a chunk of 4. "tile" uses the
ChunkOrder of a 2x2 tile, so a chunk of 4 is two pairs that are not
neighbours. Every job is pinned with -w to the node whose layout it checks.
"""

import pytest

import atf

GPUS = 8
SIZES = "2,4,8"
NEST_PAIRS = [{0, 1}, {2, 3}, {4, 5}, {6, 7}]
TILE_ORDER = "0,1,4,5,2,3,6,7"
TILE_QUADS = [{0, 1, 4, 5}, {2, 3, 6, 7}]

node = {}


@pytest.fixture(scope="module", autouse=True)
def setup():
    # slurmd parses ChunkSizes/ChunkOrder and scontrol shows them
    for component in ["sbin/slurmd", "bin/scontrol"]:
        atf.require_version(
            (26, 11),
            component,
            reason="Issue 51025: gres.conf ChunkSizes/ChunkOrder are new in 26.11",
        )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # CR_CPU keeps a single-task job off a whole socket, so several chunk jobs
    # fit at once
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(2, [("CPUs", 8), ("Gres", f"gpu:chunked:{GPUS}")])
    for i in range(GPUS):
        atf.require_tty(i)

    gpu_nodes = sorted(
        name
        for name, params in atf.get_nodes(live=False, quiet=True, fatal=True).items()
        if name != "DEFAULT" and f"gpu:chunked:{GPUS}" in str(params.get("Gres", ""))
    )
    assert len(gpu_nodes) >= 2, f"Expected two GPU nodes, got {gpu_nodes}"
    node.update(zip(["nest", "tile"], gpu_nodes))

    records = {
        "nest": f"Name=gpu Type=chunked File=/dev/tty[0-{GPUS - 1}] "
        f"ChunkSizes={SIZES}",
        "tile": f"Name=gpu Type=chunked File=/dev/tty[0-{GPUS - 1}] "
        f"ChunkSizes={SIZES} ChunkOrder={TILE_ORDER}",
    }
    atf.require_config_parameter(
        "NodeName",
        "\nNodeName=".join(f"{node[role]} {line}" for role, line in records.items()),
        source="gres",
    )
    atf.require_slurm_running()


def _run(job_args, state="RUNNING"):
    """Submit a job, wait for state, and return its ID."""

    job_id = atf.submit_job_sbatch(f"{job_args} --wrap 'sleep infinity'", fatal=True)
    assert atf.wait_for_job_state(
        job_id, state
    ), f"Job {job_id} ({job_args}) should reach {state}"
    return job_id


def _hold_pairs():
    """Hold every device of the nest node, a pair per job, keyed by the pair."""

    holders = {}
    for _ in NEST_PAIRS:
        job_id = _run(f"-N1 -w {node['nest']} --gres=gpu:chunked:2")
        holders[frozenset(atf.get_job_gres_indexes(job_id))] = job_id
    assert set(holders) == {
        frozenset(pair) for pair in NEST_PAIRS
    }, f"Expected the jobs to hold the aligned pairs {NEST_PAIRS}, got {holders}"
    return holders


@pytest.mark.parametrize(
    "role, order",
    [("nest", "0/1/2/3/4/5/6/7"), ("tile", TILE_ORDER.replace(",", "/"))],
)
def test_chunk_config_is_reported(role, order):
    name = node[role]
    assert atf.wait_for_node_state(name, "IDLE", operator=atf.SetMatch.EQUAL), (
        f"{name} should register IDLE (state="
        f"{atf.get_node_parameter(name, 'state')}, "
        f"reason={atf.get_node_parameter(name, 'reason')})"
    )
    output = atf.run_command_output(
        f"scontrol show node -d {name}", fatal=True, quiet=True
    )
    expected = f"GresChunks=gpu:chunked:sizes={SIZES.replace(',', '/')};order={order}"
    assert expected in output, f"Expected {expected} in: {output}"


def test_free_pairs_across_a_boundary_are_not_a_chunk_of_4():
    """Chunks nest, so a chunk of 4 is only ever two pairs of one chunk of 4.

    Devices 2-5 are four free neighbours, but they straddle the two chunks
    of 4, so a four device job has to wait until one of those is whole.
    """

    holders = _hold_pairs()
    atf.cancel_jobs(
        [holders[frozenset({2, 3})], holders[frozenset({4, 5})]], fatal=True
    )

    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node['nest']} --gres=gpu:chunked:4 --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="Resources"
    ), "Devices 2-5 are free but not one chunk of 4, so the job must wait"

    atf.cancel_jobs([holders[frozenset({0, 1})]], fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Freeing devices 0-1 completes the chunk of 0-3"
    indexes = atf.get_job_gres_indexes(job_id)
    assert indexes == {0, 1, 2, 3}, f"Expected the chunk {{0, 1, 2, 3}}, got {indexes}"


def test_best_fit_keeps_a_chunk_of_4_whole():
    """A pair goes into the partly used chunk of 4, not into a free one.

    With only devices 4-5 held, first fit would take 0-1 and leave no free
    chunk of 4 for the next job.
    """

    holders = _hold_pairs()
    atf.cancel_jobs(
        [job_id for pair, job_id in holders.items() if pair != {4, 5}], fatal=True
    )

    job_id = _run(f"-N1 -w {node['nest']} --gres=gpu:chunked:2")
    indexes = atf.get_job_gres_indexes(job_id)
    assert indexes == {
        6,
        7,
    }, f"Best fit should complete the chunk of 4-7, got {indexes}"

    job_id = _run(f"-N1 -w {node['nest']} --gres=gpu:chunked:4")
    indexes = atf.get_job_gres_indexes(job_id)
    assert indexes == {0, 1, 2, 3}, f"Expected the free chunk of 0-3, got {indexes}"


def test_chunk_order_forms_chunks_of_4():
    """A chunk of 4 is four consecutive chunk positions, wherever they lead."""

    allocated = [
        atf.get_job_gres_indexes(_run(f"-N1 -w {node['tile']} --gres=gpu:chunked:4"))
        for _ in TILE_QUADS
    ]
    for indexes in allocated:
        assert indexes in TILE_QUADS, f"Expected one of {TILE_QUADS}, got {indexes}"
    assert allocated[0] != allocated[1], f"Two jobs share chunk {allocated[0]}"


def test_best_fit_follows_chunk_order():
    """Best fit fills a chunk of 4 by chunk position, not by device index.

    The second pair completes the chunk of 4 the first one started, which is
    devices 4-5 here and would be 2-3 in device order.
    """

    first = atf.get_job_gres_indexes(
        _run(f"-N1 -w {node['tile']} --gres=gpu:chunked:2")
    )
    assert first == {0, 1}, f"Expected the first pair at positions 0-1, got {first}"
    second = atf.get_job_gres_indexes(
        _run(f"-N1 -w {node['tile']} --gres=gpu:chunked:2")
    )
    assert second == {4, 5}, f"Expected the pair at positions 2-3, got {second}"
