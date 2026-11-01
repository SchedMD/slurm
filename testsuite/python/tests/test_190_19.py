############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""An absent GPU is auto-drained with config_overrides on.

drain_absent_gres keeps the node up. config_overrides is on as well, and the
per-device topology must still be used, so an exclusive job gets only the
GPUs that are present. test_190_15 covers the same with it off.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"

GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"
GPU_B = "GPU-bbbbbbbb-1111-1111-1111-111111111111"
GPU_C = "GPU-cccccccc-2222-2222-2222-222222222222"

NOT_FOUND = "not_found"


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Three a100s, with config_overrides on as well as drain_absent_gres."""
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: SlurmctldParameters=drain_absent_gres added in 26.11",
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
    atf.require_config_parameter_includes("SlurmdParameters", "config_overrides")
    for tty in range(3):
        atf.require_tty(tty)
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B), (2, GPU_C))
    )
    atf.require_nodes(1, [("Gres", "gpu:a100:3"), ("CPUs", 2)])
    atf.require_slurm_running()


def test_absent_gpu_auto_drains_with_config_overrides():
    """An absent GPU is auto-drained, and an exclusive job gets only the two
    present GPUs.

    The node stays up and DEGRADED. The per-device topology is kept, so the
    job is counted against the GPUs that are there.
    """
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))
    for _ in atf.timer(fatal=True):
        detail = atf.get_node_detail(NODE)
        if bool(re.search(rf"gpu\(IDX:[^)]*\)\([^)]*{NOT_FOUND}", detail or "")):
            break

    state = atf.get_node_field(NODE, "State")
    assert "INVAL" not in state, atf.get_node_detail(NODE)
    assert "DOWN" not in state, atf.get_node_detail(NODE)
    assert "DEGRADED" in state, atf.get_node_detail(NODE)

    job_id = atf.submit_job_sbatch(
        f"-w {NODE} --exclusive --wrap 'sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"exclusive job did not run with an absent GPU: {atf.get_node_detail(NODE)}"

    detail = atf.run_command_output(
        f"scontrol -d show job {job_id}", fatal=True, quiet=True
    )
    assert re.search(r"gpu:a100:2\b", detail), f"job lost its GPUs: {detail}"
    assert not re.search(
        r"gpu:a100:3\b", detail
    ), f"job was given the absent GPU: {detail}"
