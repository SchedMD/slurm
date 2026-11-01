############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""The GPU bitmaps a shard job holds survive a slurmd registration.

The GPUs have no UUID and the shards are a count with no device file, so the
shard records carry no topology. The controller spreads the shards over the
GPUs itself, and the shard bitmap takes its width from the GPU bitmap.

A registration that changes nothing leaves both bitmaps alone, so it cannot
be refused for jobs using the shards. Cutting a GPU under a shard job aborts
the job, as gres.conf(5) warns; test_144_21 covers that.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"

SHARDS = f"NodeName={NODE} Name=shard Count=8\n"
TWO_GPUS = f"AutoDetect=off\nNodeName={NODE} Name=gpu File=/dev/tty[0-1]\n{SHARDS}"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 25180: device bitmaps kept while jobs hold them in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes(
        "SelectTypeParameters", "MULTIPLE_SHARING_GRES_PJ"
    )
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "shard")
    # config_overrides would keep a refused registration from marking the
    # node INVAL.
    atf.require_config_parameter_excludes("SlurmdParameters", "config_overrides")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_config_file("gres.conf", TWO_GPUS)
    atf.require_nodes(1, [("Gres", "gpu:2,shard:8"), ("CPUs", 2)])
    atf.require_slurm_running()


def _shard_job(count):
    job_id = atf.submit_job_sbatch(
        f"-w {NODE} --gres=shard:{count} --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return job_id


def test_unchanged_restart_keeps_a_shard_job():
    """Restarting slurmd unchanged under a shard job leaves the node in use.

    Nothing is resized, so the job's shards are not a reason to refuse the
    registration: the node stays MIXED, not INVAL or drained, and GresUsed
    still shows the job's shards.
    """
    job_id = _shard_job(2)

    atf.start_slurmd(NODE, fatal=True)
    detail = atf.get_node_detail(NODE)
    state = atf.get_node_field(NODE, "State") or ""
    assert "MIXED" in state, detail
    assert "INVAL" not in state and "DRAIN" not in state, detail
    assert "count changed" not in detail, detail
    assert re.search(r"GresUsed=\S*shard:2\b", detail), detail
    assert atf.wait_for_job_state(job_id, "RUNNING"), str(atf.get_job(job_id))
