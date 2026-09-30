############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GRES device chunks on a dynamic node.

A dynamic node (`slurmd -Z`) reads its own gres.conf and registers the chunk
layout, but the controller has no gres.conf entry to rebuild it from after a
restart.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

CHUNK_GPUS = 4
BAD_COUNT = 3
DYN_SIZES = "2"

dyn_node = "dynnode1"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # slurmd parses ChunkSizes/ChunkOrder, slurmctld allocates by them and
    # scontrol shows them
    for component in ["sbin/slurmd", "bin/scontrol"]:
        atf.require_version(
            (26, 11),
            component,
            reason="Issue 51025: gres.conf ChunkSizes/ChunkOrder are new in 26.11",
        )
    # Derive the cap from the configured nodes, or a config with more of them
    # than a hardcoded value refuses to start and looks like a chunk failure.
    # Room for the dynamic node.
    configured = atf.get_nodes(live=False, quiet=True, fatal=True)
    atf.require_config_parameter("MaxNodeCount", len(set(configured) - {"DEFAULT"}) + 1)
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    for i in range(CHUNK_GPUS):
        atf.require_tty(i)
    atf.require_config_parameter(
        "NodeName",
        f"{dyn_node} Name=gpu Type=chunked "
        f"File=/dev/tty[0-{CHUNK_GPUS - 1}] ChunkSizes={DYN_SIZES}",
        source="gres",
    )
    atf.require_slurm_running()


def _gres_chunks(node):
    output = atf.run_command_output(
        f"scontrol show node -d {node}", fatal=True, quiet=True
    )
    match = re.search(
        r"GresChunks=gpu(?::\w+)?:sizes=([^;,\s]+);order=([^;,\s]+)", output
    )
    return (match.group(1), match.group(2)) if match else (None, None)


def _wait_for_node_idle(node):
    """wait_for_node_state() fails while the node is still arriving."""

    for _ in atf.timer():
        state = atf.get_nodes(quiet=True).get(node, {}).get("state")
        if state and "IDLE" in state and "DRAIN" not in state:
            return True
    return False


def _start_dynamic_slurmd():
    """Start the dynamic slurmd and wait for it to register IDLE.

    No -b: after a slurmctld restart the node's last response is from the
    state file, so a slurmd reporting a fresh boot would be marked
    "unexpectedly rebooted" and set DOWN.
    """

    atf.start_slurmd(dyn_node, f"-Z --conf 'Gres=gpu:chunked:{CHUNK_GPUS} CPUs=8'")
    assert _wait_for_node_idle(dyn_node), f"{dyn_node} never registered IDLE"


def _stop_dynamic_slurmd():
    """Stop the dynamic slurmd, False if none was running."""

    if not atf.slurmd_pids(dyn_node):
        return False
    atf.stop_slurmd(dyn_node)
    return True


@pytest.fixture(scope="module")
def dynamic_node(setup):
    _start_dynamic_slurmd()
    yield dyn_node
    _stop_dynamic_slurmd()
    atf.run_command(
        f"scontrol delete nodename={dyn_node}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


def test_dynamic_node_registers_chunks(dynamic_node):
    assert _gres_chunks(dynamic_node) == (
        DYN_SIZES,
        "0/1/2/3",
    ), f"Expected {dynamic_node} to report its registered GresChunks"


def test_dynamic_node_enforces_chunk_sizes(dynamic_node):
    job_id = atf.submit_job_sbatch(
        f"-w {dynamic_node} --gres=gpu:chunked:2 --wrap 'true'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), "A configured chunk size should run on a dynamic node"

    error = atf.run_command_error(
        f"sbatch -w {dynamic_node} --gres=gpu:chunked:{BAD_COUNT} --wrap 'true'",
        xfail=True,
        fatal=True,
    )
    assert "Requested node configuration is not available" in error, error


def test_dynamic_node_chunks_survive_slurmctld_restart(dynamic_node):
    """Chunk state is restored from the node state file.

    The dynamic slurmd is stopped first, so it cannot re-register, and the
    controller has no gres.conf entry for the node to rebuild from.
    """

    expected = _gres_chunks(dynamic_node)
    assert _stop_dynamic_slurmd(), f"Expected a running slurmd for {dynamic_node}"
    atf.restart_slurmctld(quiet=True)
    restored = _gres_chunks(dynamic_node)
    # Put the node back, so the module-scoped fixture still holds afterwards
    _start_dynamic_slurmd()
    assert (
        restored == expected
    ), f"GresChunks after a slurmctld restart was {restored}, expected {expected}"
