############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify --gres-flags=ignore-links selects a job's GPUs without using Links.

Links are normally a preference: the devices best connected to those already
picked come first. ignore-links turns that off, leaving index order.

Two nodes of four GPUs:
  - node1: 0 linked to 3, 1 linked to 2
  - node2: no Links configured

node1's cross linking makes the flag observable: a 2-GPU job gets {0,3} by
preference and {0,1} with the links ignored.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/srun",
        reason="Issue 51169: --gres-flags=ignore-links added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="Issue 51169: --gres-flags=ignore-links added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Issue 51169: --gres-flags=ignore-links added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Overwrite rather than add to what a config variant set: adding CR_CORE to
    # a variant that already selected another CR_* type is rejected by Slurm.
    atf.require_config_parameter(
        "SelectTypeParameters", "CR_CORE,MULTIPLE_SHARING_GRES_PJ"
    )
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "shard")
    atf.require_nodes(
        2,
        [("Gres", "gpu:4,shard:8"), ("Sockets", 1), ("CoresPerSocket", 4)],
    )

    gpu_file = f"{atf.module_tmp_path}/gpu"
    for i in range(4):
        atf.run_command(f"touch {gpu_file}{i}", fatal=True)

    # node1: GPUs 0,3 linked and GPUs 1,2 linked, so the preferred second GPU
    # is not the next one by index.
    # node2: the same GPUs with no Links, i.e. no link information at all.
    atf.require_config_parameter(
        "NodeName",
        f"""node1 Name=gpu File={gpu_file}0 Links=-1,0,0,1
        NodeName=node1 Name=gpu File={gpu_file}1 Links=0,-1,1,0
        NodeName=node1 Name=gpu File={gpu_file}2 Links=0,1,-1,0
        NodeName=node1 Name=gpu File={gpu_file}3 Links=1,0,0,-1
        NodeName=node2 Name=gpu File={gpu_file}0
        NodeName=node2 Name=gpu File={gpu_file}1
        NodeName=node2 Name=gpu File={gpu_file}2
        NodeName=node2 Name=gpu File={gpu_file}3""",
        source="gres",
    )
    atf.require_slurm_running()


def _parse_idx(idx_str):
    """Expand an 'IDX' string like '0,2-3' into a set of ints."""
    indexes = set()
    for part in idx_str.split(","):
        if "-" in part:
            start, end = part.split("-")
            indexes.update(range(int(start), int(end) + 1))
        else:
            indexes.add(int(part))
    return indexes


def _allocated_gpu_indexes(node_detail):
    """Return the set of GPU indexes shown as used in 'scontrol show node -d'."""
    match = re.search(r"GresUsed=gpu:\d+\(IDX:([0-9,\-]+)\)", node_detail)
    assert match is not None, f"No used GPU index found in:\n{node_detail}"
    return _parse_idx(match.group(1))


def _run_and_get_gpus(flags, gpu_flag, node="node1"):
    """Run a job with the given flags and return the GPU indexes it was given."""
    output = atf.run_command_output(
        f"srun {flags} {gpu_flag} -w {node} scontrol show node {node} -d",
        fatal=True,
    )
    return _allocated_gpu_indexes(output)


@pytest.mark.parametrize("gpu_flag", ["--gpus-per-node=2", "--gpus=2 -N1"])
def test_default_prefers_the_linked_gpu(gpu_flag):
    """Without the flag the job gets {0,3}: the preference takes GPU 3 over the
    lower-indexed 1 and 2, which are not linked to GPU 0."""
    indexes = _run_and_get_gpus("", gpu_flag)
    assert indexes == {
        0,
        3,
    }, f"Expected the linked pair {{0, 3}}, but got GPUs {sorted(indexes)}"


@pytest.mark.parametrize("gpu_flag", ["--gpus-per-node=2", "--gpus=2 -N1"])
def test_ignore_links_takes_gpus_in_order(gpu_flag):
    """With the flag the same request gets {0,1}, leaving the linked pair for a
    job that would benefit from it."""
    indexes = _run_and_get_gpus("--gres-flags=ignore-links", gpu_flag)
    assert indexes == {
        0,
        1,
    }, f"ignore-links {gpu_flag} got GPUs {sorted(indexes)}, not {{0, 1}}"


@pytest.mark.parametrize(
    "gpu_flag",
    [
        "--gpus-per-task=2 -n1",
        "--tres-per-task=gres/gpu:2 -n1",
        "--gpus-per-socket=2 --sockets-per-node=1 -N1",
    ],
)
def test_ignore_links_takes_gpus_in_order_all_request_forms(gpu_flag):
    """The flag applies to every request form, so each gets {0,1}."""
    indexes = _run_and_get_gpus("--gres-flags=ignore-links", gpu_flag)
    assert indexes == {
        0,
        1,
    }, f"ignore-links {gpu_flag} got GPUs {sorted(indexes)}, not {{0, 1}}"


def test_ignore_links_three_gpus():
    """A 3-GPU job takes {0,1,2} rather than dropping the least connected."""
    indexes = _run_and_get_gpus("--gres-flags=ignore-links", "--gpus=3 -N1")
    assert indexes == {
        0,
        1,
        2,
    }, f"ignore-links got GPUs {sorted(indexes)}, not {{0, 1, 2}}"


def test_ignore_links_single_gpu():
    """A single-GPU request has nothing to choose between, and still runs."""
    indexes = _run_and_get_gpus("--gres-flags=ignore-links", "--gpus-per-node=1")
    assert indexes == {0}, f"a 1-GPU job got GPUs {sorted(indexes)}, not {{0}}"


def test_ignore_links_without_link_info():
    """node2 has no Links, so there is nothing for the flag to ignore. The
    request must still run, and take the GPUs in order."""
    indexes = _run_and_get_gpus(
        "--gres-flags=ignore-links", "--gpus-per-node=2", node="node2"
    )
    assert indexes == {
        0,
        1,
    }, f"ignore-links on a node with no links got GPUs {sorted(indexes)}"


def test_ignore_links_shared_gres():
    """Shared GRES are slices of one device and have no links, so the flag does
    not apply and must not stop the request."""
    job_id = atf.submit_job_sbatch(
        '--gres-flags=ignore-links --gres=shard:4 -w node1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "ignore-links must not block a shared-GRES (shard) request"


def test_ignore_links_reported_by_scontrol_and_json():
    """The flag is echoed back as GresIgnoreLinks, and as GRES_LINKS_IGNORED in
    the data parser JSON."""
    job_id = atf.submit_job_sbatch(
        '--gres-flags=ignore-links --gpus-per-node=2 -w node1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(job_id, "RUNNING"), "ignore-links job should run"
    assert (
        atf.get_job_parameter(job_id, "GresIgnoreLinks") == "Yes"
    ), "scontrol show job should report GresIgnoreLinks=Yes"
    flags = atf.get_jobs(job_id, use_json=True)[job_id]["flags"]
    assert (
        "GRES_LINKS_IGNORED" in flags
    ), f"the JSON job flags should name ignore-links, got {flags}"


def test_ignore_links_and_enforce_links_rejected():
    """The two flags are opposites, so asking for both is rejected."""
    result = atf.run_command(
        "sbatch --gres-flags=enforce-links,ignore-links --gpus-per-node=2 "
        '--wrap "true"',
        xfail=True,
    )
    assert result["exit_code"] != 0, "enforce-links with ignore-links must be rejected"
    assert (
        "mutually exclusive" in result["stderr"]
    ), f"expected a mutually exclusive rejection, got: {result['stderr']}"
