############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test per-device typed shard counts in gres.conf (Type without File)."""

import re

import pytest

import atf

node = "node1"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Issue 51032: per-device typed shard counts were added in 26.11",
    )

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter_includes("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "shard")

    for tty_num in range(6):
        atf.require_tty(tty_num)

    # Three GPU models: 2x a100 (32 shards each), 2x l40s (16 each), and
    # 2x h100 with no shard config, to check an unsharded model does not drain
    atf.require_config_parameter(
        "Name",
        "gpu Type=a100 File=/dev/tty[0-1]"
        "\nName=gpu Type=l40s File=/dev/tty[2-3]"
        "\nName=gpu Type=h100 File=/dev/tty[4-5]"
        "\nName=shard Type=a100 Count=32"
        "\nName=shard Type=l40s Count=16",
        source="gres",
    )

    atf.require_nodes(
        1,
        [
            ("Gres", "gpu:a100:2,gpu:l40s:2,gpu:h100:2,shard:a100:64,shard:l40s:32"),
            ("CPUs", 4),
            ("RealMemory", 100),
        ],
    )
    atf.require_slurm_running()


def test_node_registers_typed_totals():
    """The node registers the per-type shard totals (per-device Count times
    the number of devices of that type)."""
    output = atf.run_command_output(f"scontrol show node {node}", fatal=True)
    gres = re.search(r"Gres=(\S*)", output).group(1)
    assert "shard:a100:64" in gres, f"Expected shard:a100:64 in Gres: {gres}"
    assert "shard:l40s:32" in gres, f"Expected shard:l40s:32 in Gres: {gres}"


def test_per_device_counts():
    """Each a100 device holds 32 shards: two concurrent jobs asking for 32
    a100 shards each must both run (one per device), since shards must fit
    a single device. Before 26.11 typed shard lines without File were
    rejected at slurmd startup."""
    # 33 shards fit the 64 free a100 total but not one 32-shard device
    result = atf.run_command(
        f"srun -w {node} --gres=shard:a100:33 --mem=10 -n1 -t1 --immediate=5 true",
        xfail=True,
    )
    assert result["exit_code"] != 0, "33 shards must not fit a single a100 device"

    job_ids = []
    for i in range(2):
        job_ids.append(
            atf.submit_job_sbatch(
                f"-w {node} --gres=shard:a100:32 -n1 -t2 --mem=10 "
                f"-o shard_a_{i}.out --wrap 'echo SHARDS=$SLURM_SHARDS_ON_NODE; sleep infinity'",
                fatal=True,
            )
        )
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Both a100 devices must be exactly full: allocations may not span devices
    output = atf.run_command_output(f"scontrol show node -d {node}", fatal=True)
    assert "shard:a100:64(32/32,32/32)" in output, f"Unexpected GresUsed: {output}"

    # An l40s device holds 16 shards, so 16 fit alongside the a100 jobs
    job_b = atf.submit_job_sbatch(
        f"-w {node} --gres=shard:l40s:16 -n1 -t2 --mem=10 "
        "-o shard_b.out --wrap 'echo SHARDS=$SLURM_SHARDS_ON_NODE; sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_b, "RUNNING", fatal=True)

    for i in range(2):
        atf.assert_file_contents(f"shard_a_{i}.out", "SHARDS=32", contains=True)
    atf.assert_file_contents("shard_b.out", "SHARDS=16", contains=True)


def test_unsharded_gpu_type_does_not_drain():
    """A gpu model with no shard config (h100) must not drain the node.
    The drain triggers when: (1) gres.conf defines h100 gpus, (2) it has no
    Name=shard Type=h100 line, and (3) another model uses a typed no-File shard
    line (Name=shard Type=a100). slurmd must not then report a shard:h100:0 that
    the controller rejects as 'reported but not configured'."""

    output = atf.run_command_output(f"scontrol show node {node}", fatal=True)
    state = re.search(r"State=(\S+)", output).group(1)

    assert "DRAIN" not in state, f"node drained: {output}"
    assert "INVALID_REG" not in state, f"invalid registration: {output}"
    assert "shard:h100" not in output, f"phantom shard:h100 present: {output}"
