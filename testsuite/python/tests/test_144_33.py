############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GRES device chunks are refused on a shared GRES (shard, mps).

Shared allocations are tracked on the shared GRES, so the chunk selection
cannot see the devices they consume. A node allowing both would hand out a
chunk whose devices are already carved up, and a chunk job and a shared job
would silently hold the same device. That is a double-booking, not a
scheduling inefficiency, so the node must drain instead.

One node shares its GPUs as shard and another as mps. Both start without
ChunkSizes so that Slurm comes up healthy, and the failing configuration is
applied afterwards by restarting only that node's slurmd.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

CHUNK_GPUS = 4
SHARED_PER_GPU = {"shard": 2, "mps": 100}

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
    for shared in SHARED_PER_GPU:
        atf.require_config_parameter_includes("GresTypes", shared)
    atf.require_nodes(2, [("CPUs", 8), ("Gres", f"gpu:{CHUNK_GPUS}")])
    for i in range(CHUNK_GPUS):
        atf.require_tty(i)

    gpu_nodes = sorted(
        name
        for name, params in atf.get_nodes(live=False, quiet=True, fatal=True).items()
        if name != "DEFAULT" and f"gpu:{CHUNK_GPUS}" in str(params.get("Gres", ""))
    )
    assert len(gpu_nodes) >= 2, f"Expected two GPU nodes, got {gpu_nodes}"
    node.update(zip(SHARED_PER_GPU, gpu_nodes))
    for shared, per_gpu in SHARED_PER_GPU.items():
        atf.set_node_parameter(
            node[shared], "Gres", f"gpu:{CHUNK_GPUS},{shared}:{CHUNK_GPUS * per_gpu}"
        )

    atf.require_config_parameter("NodeName", _gres_conf(), source="gres")
    atf.require_slurm_running()


def _gres_conf(target=None, gpu_params="", shared_params=""):
    """Return the gres.conf body, chunk free except on the target node."""

    lines = []
    for shared, per_gpu in SHARED_PER_GPU.items():
        name = node[shared]
        gpu_extra = gpu_params if shared == target else ""
        shared_extra = shared_params if shared == target else ""
        lines += [
            f"{name} Name=gpu File=/dev/tty[0-{CHUNK_GPUS - 1}] {gpu_extra}",
            f"{name} Name={shared} Count={CHUNK_GPUS * per_gpu} "
            f"File=/dev/tty[0-{CHUNK_GPUS - 1}] {shared_extra}",
        ]
    return "\nNodeName=".join(line.rstrip() for line in lines)


def _write_gres_conf(body):
    """Rewrite gres.conf without reconfiguring the controller."""

    atf.run_command(
        f"cat > {atf.properties['slurm-config-dir']}/gres.conf",
        input=f"NodeName={body}\n",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


def _reload_slurmd(name, body):
    """Rewrite gres.conf and restart only that node's slurmd.

    The caller polls for the result it expects. Waiting for a registration
    here instead can miss one: slurmd_start_time has one second resolution,
    so a restart right after the previous start looks unchanged.
    """

    _write_gres_conf(body)
    assert atf.slurmd_pids(name), f"Expected a slurmd running for {name}"
    atf.start_slurmd(name, wait="process", quiet=True)


@pytest.fixture
def restore_gres_conf(request):
    """Put the chunk free config back and resume the node."""

    name = node[request.node.callspec.params["shared"]]
    atf.wait_for_node_state(name, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True)

    yield

    _reload_slurmd(name, _gres_conf())
    atf.run_command(
        f"scontrol update nodename={name} state=resume",
        user=atf.properties["slurm-user"],
        quiet=True,
        fatal=True,
    )


@pytest.mark.parametrize(
    "shared, gpu_params, shared_params",
    [
        ("shard", "ChunkSizes=2", ""),
        ("shard", "", "ChunkSizes=2"),
        ("mps", "ChunkSizes=2", ""),
        ("mps", "", "ChunkSizes=2"),
    ],
    ids=["shard_on_gpu", "shard_on_shard", "mps_on_gpu", "mps_on_mps"],
)
def test_chunks_with_shared_gres_drain_node(
    restore_gres_conf, shared, gpu_params, shared_params
):
    """ChunkSizes on a shared GRES, or on a GRES carved into one, must drain.

    Accepting it would let the chunk selection hand out devices the shared
    state already considers allocated, so two jobs would hold the same device
    without either being able to see the other.
    """

    name = node[shared]
    _reload_slurmd(name, _gres_conf(shared, gpu_params, shared_params))

    assert atf.wait_for_node_state(name, "DRAIN"), (
        f"Node {name} should drain when ChunkSizes is set on a shared "
        f"GRES or on a GRES carved into one "
        f"(gpu: '{gpu_params}', {shared}: '{shared_params}') "
        f"(state={atf.get_node_parameter(name, 'state')}, "
        f"reason={atf.get_node_parameter(name, 'reason')})"
    )

    reason = str(atf.get_node_parameter(name, "reason"))
    assert "chunk" in reason.lower(), (
        f"The drain reason should name the chunk rule that failed, so an "
        f"admin can find it in the documented list. Got: {reason}"
    )
