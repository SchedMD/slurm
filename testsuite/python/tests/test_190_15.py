############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""An exclusive job gets the GPUs that are still present when one is absent.

drain_absent_gres keeps the node up. The node has two GPU types, so the
absent a100 must not be counted in the type's grant. test_190_19 covers the
same with config_overrides on.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"

GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"
GPU_B = "GPU-bbbbbbbb-1111-1111-1111-111111111111"
GPU_C = "GPU-cccccccc-2222-2222-2222-222222222222"
GPU_D = "GPU-dddddddd-3333-3333-3333-333333333333"

NOT_FOUND = "not_found"


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Two a100s and two h100s, with config_overrides off."""
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
    atf.require_config_parameter_excludes("SlurmdParameters", "config_overrides")
    for tty in range(4):
        atf.require_tty(tty)
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_config_file(
        "fake_gpus.conf",
        atf.fake_gpus_conf(
            (0, "a100", GPU_A),
            (1, "a100", GPU_B),
            (2, "h100", GPU_C),
            (3, "h100", GPU_D),
        ),
    )
    atf.require_nodes(1, [("Gres", "gpu:a100:2,gpu:h100:2"), ("CPUs", 2)])
    atf.require_slurm_running()


def test_whole_node_job_gets_every_present_gpu():
    """An exclusive job gets the one present a100 and both h100s."""
    atf.restart_slurmd_with_fake_gpus(
        NODE, (0, "a100", GPU_A), (2, "h100", GPU_C), (3, "h100", GPU_D)
    )
    for _ in atf.timer(fatal=True):
        detail = atf.get_node_detail(NODE)
        if bool(re.search(rf"gpu\(IDX:[^)]*\)\([^)]*{NOT_FOUND}", detail or "")):
            break

    job_id = atf.submit_job_sbatch(
        f"-w {NODE} --exclusive --wrap 'sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"exclusive job did not run with an absent GPU: {atf.get_node_detail(NODE)}"

    detail = atf.run_command_output(
        f"scontrol -d show job {job_id}", fatal=True, quiet=True
    )
    assert re.search(r"gpu:h100:2\b", detail), f"job lost its h100s: {detail}"
    assert re.search(r"gpu:a100:1\b", detail), f"job lost its a100: {detail}"
    assert not re.search(
        r"gpu:a100:2\b", detail
    ), f"job was given the absent a100: {detail}"
