############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Drained shared GRES on dynamic-normal slurmds (`slurmd -Z`).

Shard and MPS cannot be drained directly. Drain the GPU that backs them.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# Node names need to be in the form of nodeNN for the s2n variant.
DYN_NODE = "node100"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: GRES drain added in 26.11",
    )
    atf.require_version(
        (26, 5),
        component="sbin/slurmd",
        reason="Issue 50653: gres.conf Autodetect=full added in 26.05",
    )
    atf.require_config_parameter("MaxNodeCount", 4)
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes(
        "SelectTypeParameters", "MULTIPLE_SHARING_GRES_PJ"
    )
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "shard")
    atf.require_config_parameter_includes("GresTypes", "mps")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_slurm_running()


def _shard_node(dynamic_slurmds):
    """Configure two sharing a100 GPUs with 100 shards each and start the node."""
    atf.require_config_file(
        "gres.conf",
        "Autodetect=full\n"
        f"NodeName={DYN_NODE} Name=gpu File=/dev/tty0 Type=a100\n"
        f"NodeName={DYN_NODE} Name=gpu File=/dev/tty1 Type=a100\n"
        f"NodeName={DYN_NODE} Name=shard Count=100 File=/dev/tty0\n"
        f"NodeName={DYN_NODE} Name=shard Count=100 File=/dev/tty1\n",
    )
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, "(null)"), (1, "(null)"))
    )
    atf.restart_slurm(clean=True, quiet=True)

    return dynamic_slurmds(DYN_NODE, "Gres=gpu:a100:2,shard:200")


def _mps_node(dynamic_slurmds):
    """Two a100 GPUs, 100 MPS contexts each."""
    atf.require_config_file(
        "gres.conf",
        "Autodetect=full\n"
        f"NodeName={DYN_NODE} Name=gpu File=/dev/tty0 Type=a100\n"
        f"NodeName={DYN_NODE} Name=gpu File=/dev/tty1 Type=a100\n"
        f"NodeName={DYN_NODE} Name=mps Count=100 File=/dev/tty0\n"
        f"NodeName={DYN_NODE} Name=mps Count=100 File=/dev/tty1\n",
    )
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, "(null)"), (1, "(null)"))
    )
    atf.restart_slurm(clean=True, quiet=True)
    return dynamic_slurmds(DYN_NODE, "Gres=gpu:a100:2,mps:200")


def test_gres_drain_gpu_excludes_shared_shard(dynamic_slurmds):
    """Draining a sharing GPU removes its shared shards from scheduling."""
    node = _shard_node(dynamic_slurmds)
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="test")
    assert result["exit_code"] == 0, "command should succeed"

    job_id = atf.submit_job_sbatch(
        f"-w {node} --gres=shard:50 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)
    job = atf.get_job(job_id)
    assert re.search(r"shard:50\(0/100,50/100\)", str(job)), job
    node_detail = atf.get_node_detail(node)
    assert re.search(
        r"GresUsed=.*shard:a100:50\(d/100,50/100\)", node_detail
    ), node_detail


def test_gres_drain_shared_gres_rejected(dynamic_slurmds):
    """scontrol rejects draining shared GRES; drain the sharing device instead."""
    node = _shard_node(dynamic_slurmds)
    result = atf.update_tres_drain(node, "gres/shard", "Index=ALL", reason="test")
    assert result["exit_code"] != 0, result
    assert "Shared GRES" in result["stderr"], result
    assert "sharing GRES" in result["stderr"], result


def test_gres_resume_sharing_gpu_restores_shards(dynamic_slurmds):
    """Resuming a drained sharing GPU returns its shards to scheduling."""
    node = _shard_node(dynamic_slurmds)

    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="test")
    assert result["exit_code"] == 0, result
    # One sharing GPU's shards are excluded (drained 'd/' marker present).
    detail = atf.get_node_detail(node)
    assert re.search(r"GresUsed=.*shard:a100:\d+\(d/100", detail), detail

    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", action="Resume")
    assert result["exit_code"] == 0, result

    # After resume the node leaves DEGRADED and the drained marker is gone.
    assert atf.wait_for_node_state(
        node, "DEGRADED", reverse=True
    ), f"node still DEGRADED after resume: {atf.get_node_parameter(node, 'state')!r}"
    detail = atf.get_node_detail(node)
    assert "(d/" not in detail, detail

    # All 200 shards are schedulable again (a 150-shard job needs both GPUs).
    job_id = atf.submit_job_sbatch(
        f"-w {node} --gres=shard:150 --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)


def test_held_shards_on_a_drained_gpu_render_count(dynamic_slurmds):
    """Shards a job still holds on a drained GPU render as dN/M."""
    node = _shard_node(dynamic_slurmds)
    job_id = atf.submit_job_sbatch(
        f"-w {node} --gres=shard:150 --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)
    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="held", fatal=True)
    detail = atf.get_node_detail(node)
    assert re.search(r"GresUsed=.*shard:a100:\d+\(d100/100,50/100\)", detail), detail


def test_drain_mps_directly_rejected(dynamic_slurmds):
    """scontrol rejects a drain of gres/mps; drain the GPU under it."""
    node = _mps_node(dynamic_slurmds)
    result = atf.update_tres_drain(node, "gres/mps", "Index=ALL", reason="mps")
    assert result["exit_code"] != 0, result
    assert "Shared GRES" in result["stderr"], result
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)


def test_draining_gpu_removes_its_mps(dynamic_slurmds):
    """Draining a GPU takes its MPS contexts out of scheduling."""
    node = _mps_node(dynamic_slurmds)
    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="mps", fatal=True)

    job_id = atf.submit_job_sbatch(
        f"-w {node} --gres=mps:50 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    job = atf.get_job(job_id)
    assert re.search(r"mps:50\(0/100,50/100\)", str(job)), job
    detail = atf.get_node_detail(node)
    assert re.search(r"GresUsed=.*mps:a100:50\(d/100,50/100\)", detail), detail
