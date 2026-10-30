############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GRES device chunks on a saved UUID order.

Every GPU reports a UUID, so the controller saves that order and builds the
chunk domains in it. ChunkSizes=2 pairs indexes 0-1 and 2-3. An absent GPU
stays in its pair, drained, so the node stays up and a job of two GPUs is
given the pair that has no absent GPU.

slurmd reports a gres.conf File= line it does not detect as a device with
no UUID, which is not part of the saved order. So the absent GPU is left
out of gres.conf as well as fake_gpus.conf.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"
GPUS = 4

ALL_GPUS = (
    (0, "(null)", "GPU-aaaaaaaa-0000-0000-0000-000000000000"),
    (1, "(null)", "GPU-bbbbbbbb-1111-1111-1111-111111111111"),
    (2, "(null)", "GPU-cccccccc-2222-2222-2222-222222222222"),
    (3, "(null)", "GPU-dddddddd-3333-3333-3333-333333333333"),
)
CHUNKS = re.compile(r"GresChunks=gpu:sizes=2;order=0/1/2/3\b")


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: stable GRES device slots added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots keyed by UUID added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("SlurmctldParameters", "drain_absent_gres")
    atf.require_config_parameter_excludes("SlurmdParameters", "config_overrides")
    for i in range(GPUS):
        atf.require_tty(i)
    atf.require_config_file("gres.conf", _gres_conf(GPUS))
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf(*ALL_GPUS))
    atf.require_nodes(1, [("Gres", f"gpu:{GPUS}"), ("CPUs", GPUS)])
    atf.require_slurm_running()


@pytest.fixture(autouse=True)
def reset():
    """Start each test with every GPU present and nothing drained."""
    _register(*ALL_GPUS)
    atf.update_tres_drain(NODE, "gres/gpu", "Index=ALL", action="Resume", quiet=True)
    for _ in atf.timer(fatal=True):
        if atf.get_node_tres_drained(NODE) is None:
            break


def _gres_conf(count):
    return f"NodeName={NODE} Name=gpu File=/dev/tty[0-{count - 1}] ChunkSizes=2\n"


def _register(*devices):
    """Restart slurmd with exactly these GPUs in gres.conf and detected."""
    atf.require_config_file("gres.conf", _gres_conf(len(devices)))
    atf.restart_slurmd_with_fake_gpus(NODE, *devices)


def _chunks():
    output = atf.run_command_output(
        f"scontrol show node -d {NODE}", fatal=True, quiet=True
    )
    return CHUNKS.search(output), output


def test_chunks_follow_device_slots():
    """The chunk settings of gres.conf are kept on a node with device slots."""

    found, output = _chunks()
    assert found, f"Expected ChunkSizes=2 over slots 0-3: {output}"


def test_absent_gpu_stays_in_its_chunk():
    """The node stays up with its chunks, and a job gets the whole pair."""

    _register(*ALL_GPUS[:3])
    for _ in atf.timer(fatal=True):
        drained = atf.get_node_tres_drained(NODE)
        if drained and "IDX:3" in drained and "not_found" in drained:
            break
    found, output = _chunks()
    assert found, f"Expected the chunks to keep the absent slot: {output}"

    pair = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:2 --wrap 'sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(pair, "RUNNING"), atf.get_node_detail(NODE)
    assert atf.get_job_gres_indexes(pair) == {0, 1}, atf.get_job(pair)

    split = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:2 --wrap 'sleep infinity'", fatal=True
    )
    assert atf.job_stays_pending(
        split
    ), f"A job of 2 GPUs was given the pair with the absent GPU: {atf.get_job(split)}"
