############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A drain follows its GPU when the configured set shrinks.

Every GPU reports a UUID. Drain GPU_B, then drop GPU_A and one configured
GPU: the drain stays on GPU_B at the index slurmd now gives it. Configuring
GPU_B away drops that drain. A GPU that replaces a drained one is not held
by the old drain, so a job can use every present GPU.

This uses a STATIC node because dynamic-normal (`slurmd -Z`) nodes reject a
live GRES reconfig (INVALID_REG); the dynamic UUID coverage lives in
test_190_7. Reducing the node's configured GRES and fake_gpus and restarting
Slurm (state preserved via clean=False) is what changes the saved order.

The same drain is issued once by Uuid= and once by Index=.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"  # require_nodes(1, ...) yields node1

# Fake GPU device UUIDs, in fake_gpus.conf device order.
GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"
GPU_B = "GPU-bbbbbbbb-1111-1111-1111-111111111111"
GPU_C = "GPU-cccccccc-2222-2222-2222-222222222222"
GPU_D = "GPU-dddddddd-3333-3333-3333-333333333333"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_tty(2)
    atf.require_tty(3)
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B), (2, GPU_C))
    )
    atf.require_nodes(1, [("Gres", "gpu:a100:3"), ("CPUs", 2)])
    atf.require_version(
        (26, 11), "bin/scontrol", reason="Ticket 25180: TRES drain added in 26.11"
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots keyed by UUID added in 26.11",
    )
    atf.require_slurm_running()


@pytest.mark.parametrize("selector", [f"Uuid={GPU_B}", "Index=1"])
def test_uuid_drain_follows_gpu_across_index_shift(selector):
    """Drain GPU_B (by UUID or by its index), drop GPU_A and one configured
    GPU, and confirm the drain follows GPU_B. Once GPU_B itself is configured
    away the drain is dropped, and it stays gone when GPU_B returns."""

    _reset_three_gpus()

    # Devices A@0, B@1, C@2. Drain the middle GPU.
    result = atf.update_tres_drain(NODE, "gres/gpu", selector, reason="followme")
    assert result["exit_code"] == 0, result
    assert re.search(
        r"gpu\(IDX:1\)\(followme\)", atf.get_node_detail(NODE)
    ), atf.get_node_detail(NODE)

    # Drop GPU_A and configure two GPUs. slurmd numbers GPU_B 0 and GPU_C 1.
    # The drain stays on GPU_B, so the index shown for it becomes 0, and
    # GPU_C must NOT be drained.
    atf.set_gpu_set(NODE, atf.fake_gpus_conf((1, GPU_B), (2, GPU_C)), 2)
    for _ in atf.timer(timeout=60, fatal=True):
        d = atf.get_node_tres_drained(NODE)
        if d == "gpu(IDX:0)(followme)":
            break

    # Configure GPU_B away. Its drain is dropped with it.
    atf.set_gpu_set(NODE, atf.fake_gpus_conf((2, GPU_C)), 1)

    def _b_dropped():
        detail = atf.get_node_detail(NODE) or ""
        if not re.search(r"\bGres=gpu:a100:1\b", detail):
            return False
        return atf.get_node_tres_drained(NODE) is None

    for _ in atf.timer(timeout=60, fatal=True):
        done = _b_dropped()
        if done:
            break
    state = atf.get_node_parameter(NODE, "state") or ""
    assert "DEGRADED" not in state, atf.get_node_detail(NODE)
    assert "DRAIN" not in state, atf.get_node_detail(NODE)

    # GPU_B returns after GPU_C. The dropped drain does not come back.
    atf.set_gpu_set(NODE, atf.fake_gpus_conf((2, GPU_C), (3, GPU_B)), 2)

    def _b_back_undrained():
        detail = atf.get_node_detail(NODE) or ""
        if not re.search(r"\bGres=gpu:a100:2\b", detail):
            return False
        return atf.get_node_tres_drained(NODE) is None

    for _ in atf.timer(timeout=60, fatal=True):
        done = _b_back_undrained()
        if done:
            break


def _reset_three_gpus():
    """Restore GPU_A, GPU_B and GPU_C at indexes 0-2 with nothing drained."""
    atf.set_gpu_set(NODE, atf.fake_gpus_conf((0, GPU_A), (1, GPU_B), (2, GPU_C)), 3)
    # A test can leave a different saved order. The report above is then
    # refused, and State=RESUME drops that order. Register again so the
    # controller saves [A, B, C].
    atf.run_command(
        f"scontrol update NodeName={NODE} State=RESUME",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.start_slurmd(NODE)
    atf.update_tres_drain(NODE, "gres/gpu", "Uuid=ALL", action="Resume", fatal=True)
    for _ in atf.timer(timeout=60, fatal=True):
        f = atf.get_node_tres_drained(NODE)
        if f is None:
            break


@pytest.mark.parametrize("job_args", ["--exclusive -N1 --gres=gpu:a100:3", "--gpus=3"])
def test_replacement_gpu_is_allocatable(job_args):
    """A GPU that replaces a drained one is not held back by the old drain.

    GPU_D takes GPU_B's slot. GPU_B's drain is dropped, so the node has three
    present, undrained GPUs, and a job asking for all of them must get all three.
    """

    _reset_three_gpus()
    result = atf.update_tres_drain(NODE, "gres/gpu", f"Uuid={GPU_B}", reason="replaced")
    assert result["exit_code"] == 0, result

    atf.set_gpu_set(NODE, atf.fake_gpus_conf((0, GPU_A), (1, GPU_D), (2, GPU_C)), 3)

    def _replacement_undrained():
        detail = atf.get_node_detail(NODE) or ""
        if not re.search(r"\bGres=gpu:a100:3\b", detail):
            return False
        if GPU_B in (atf.get_node_tres_drained(NODE) or ""):
            return False
        return atf.get_node_tres_drained(NODE) is None

    for _ in atf.timer(timeout=60, fatal=True):
        done = _replacement_undrained()
        if done:
            break

    job_id = atf.submit_job_sbatch(
        f"-w {NODE} {job_args} --wrap 'sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"job did not run on the three present GPUs: {atf.get_node_detail(NODE)}"
    detail = atf.run_command_output(
        f"scontrol -d show job {job_id}", fatal=True, quiet=True
    )
    assert re.search(
        r"GRES=gpu:a100:3\(IDX:0-2\)", detail
    ), f"job did not get all three GPUs: {detail}"
