############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify --gres-flags=enforce-links requires a job's GPUs to be linked.

Every GPU the job gets on a node must be linked to all the others. A node that
cannot supply such a set is not used: a request no node can satisfy is rejected
at submission, one whose set is busy pends.

Four nodes of four GPUs:
  - node1: linked pairs {0,1} and {2,3}
  - node2: no Links configured
  - node3: only {2,3} linked, so an enforce-links job is forced off the first
    GPUs, which index order alone would have picked
  - node4: typed devices, where the only linked pair {1,3} spans the two
    Types and neither Type holds a linked pair of its own

Each case is paired with a counter-example that still runs without the flag.
Shards cover the shared-GRES exemption.
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
        reason="Issue 51169: --gres-flags=enforce-links added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="Issue 51169: --gres-flags=enforce-links added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Issue 51169: --gres-flags=enforce-links added in 26.11",
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
        4,
        [("Gres", "gpu:4,shard:8"), ("Sockets", 1), ("CoresPerSocket", 4)],
    )
    # node4's GPUs are typed. A typed request (gpu:a100:2) is matched against
    # the typed GRES declared on the node's slurm.conf line, so replace the
    # generic gpu:4 there with the per-Type breakdown. (Editing the existing
    # line with set_node_parameter keeps the other NodeName lines, which
    # require_config_parameter's dict form would drop.)
    atf.set_node_parameter("node4", "Gres", "gpu:a100:2,gpu:v100:2,shard:8")

    gpu_file = f"{atf.module_tmp_path}/gpu"
    for i in range(4):
        atf.run_command(f"touch {gpu_file}{i}", fatal=True)

    # node1: two linked pairs (GPUs 0,1 linked; GPUs 2,3 linked).
    # node2: the same GPUs with no Links, i.e. no link information at all.
    # node3: only GPUs 2,3 are linked; GPUs 0 and 1 are linked to nothing.
    # node4: typed GPUs, and only the cross-Type pair {1,3} is linked. The
    # Links rows are node-global: four entries each, whatever the Type, so a
    # typed request must be read against the type's devices at their global
    # positions, not against type-local indexes 0 and 1.
    atf.require_config_parameter(
        "NodeName",
        f"""node1 Name=gpu File={gpu_file}0 Links=-1,1,0,0
        NodeName=node1 Name=gpu File={gpu_file}1 Links=1,-1,0,0
        NodeName=node1 Name=gpu File={gpu_file}2 Links=0,0,-1,1
        NodeName=node1 Name=gpu File={gpu_file}3 Links=0,0,1,-1
        NodeName=node2 Name=gpu File={gpu_file}0
        NodeName=node2 Name=gpu File={gpu_file}1
        NodeName=node2 Name=gpu File={gpu_file}2
        NodeName=node2 Name=gpu File={gpu_file}3
        NodeName=node3 Name=gpu File={gpu_file}0 Links=-1,0,0,0
        NodeName=node3 Name=gpu File={gpu_file}1 Links=0,-1,0,0
        NodeName=node3 Name=gpu File={gpu_file}2 Links=0,0,-1,1
        NodeName=node3 Name=gpu File={gpu_file}3 Links=0,0,1,-1
        NodeName=node4 Name=gpu Type=a100 File={gpu_file}0 Links=-1,0,0,0
        NodeName=node4 Name=gpu Type=a100 File={gpu_file}1 Links=0,-1,0,1
        NodeName=node4 Name=gpu Type=v100 File={gpu_file}2 Links=0,0,-1,0
        NodeName=node4 Name=gpu Type=v100 File={gpu_file}3 Links=0,1,0,-1""",
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
    """Return the set of GPU indexes shown as used in 'scontrol show node -d'.

    Typed devices report one GresUsed entry per Type, so gather them all."""
    gres_used = re.search(r"GresUsed=(\S+)", node_detail)
    assert gres_used is not None, f"No GresUsed found in:\n{node_detail}"
    matches = re.findall(r"gpu:[^(]*\(IDX:([0-9,\-]+)\)", gres_used.group(1))
    assert matches, f"No used GPU index found in:\n{node_detail}"
    indexes = set()
    for idx in matches:
        indexes |= _parse_idx(idx)
    return indexes


def _job_gpu_indexes(job_id):
    """GPU indexes allocated to a job, over every node.

    One "Nodes=... GRES=" line per group of nodes, so reading only the first
    would drop the GPUs given on the others.
    """
    out = atf.run_command_output(f"scontrol show job {job_id} -d", fatal=True)
    matches = re.findall(r"GRES=gpu:[^(]*\(IDX:([0-9,\-]+)\)", out)
    assert matches, f"No GPU IDX for job {job_id}:\n{out}"
    indexes = set()
    for idx in matches:
        indexes |= _parse_idx(idx)
    return indexes


def _node_used_gpus(node):
    """Return the set of GPU indexes in use on a node, empty when none are."""
    out = atf.run_command_output(f"scontrol show node {node} -d", fatal=True)
    match = re.search(r"GresUsed=gpu:\d+\(IDX:([0-9,\-]+)\)", out)
    if not match:
        return set()
    return _parse_idx(match.group(1))


def _leave_idle_cross_pair_on_node1():
    """Leave node1 with only the cross pair {0,2} idle.

    Hold all four GPUs, then free 0 and 2. Their partners 1 and 3 stay busy,
    so the idle pair is not linked. Returns {gpu: job id} still running.
    """
    holder = {}
    for _ in range(4):
        job_id = atf.submit_job_sbatch(
            '--gpus=1 -w node1 --wrap "sleep infinity"', fatal=True
        )
        assert atf.wait_for_job_state(
            job_id, "RUNNING", fatal=True
        ), f"holder job {job_id} should run"
        idx = _job_gpu_indexes(job_id)
        assert len(idx) == 1, f"holder {job_id} took {idx}"
        holder[next(iter(idx))] = job_id
    assert set(holder) == {0, 1, 2, 3}, f"all four GPUs occupied: {holder}"
    for gpu in (0, 2):
        atf.run_command(f"scancel {holder[gpu]}", fatal=True)
    # JobState reads CANCELLED while the job is still completing, at which
    # point its GPUs are still allocated, so wait on the node's own used set.
    for _ in atf.timer(fatal=True):
        if _node_used_gpus("node1") == {1, 3}:
            break
    return {gpu: job_id for gpu, job_id in holder.items() if gpu in (1, 3)}


def test_two_gpu_job_prefers_a_linked_pair():
    """Without the flag a 2-GPU job still prefers a linked pair, by the default
    link-count sort. A preference, not the requirement tested below."""
    output = atf.run_command_output(
        "srun --gpus-per-node=2 -w node1 scontrol show node node1 -d",
        fatal=True,
    )
    indexes = _allocated_gpu_indexes(output)
    assert indexes in (
        {0, 1},
        {2, 3},
    ), f"Expected a linked GPU pair, but got GPUs {sorted(indexes)}"


@pytest.mark.parametrize(
    "gpu_flag",
    [
        "--gpus=2 -N1",
        "--gpus-per-node=2 -N1",
        "--gpus-per-task=2 -n1",
        "--tres-per-task=gres/gpu:2 -n1",
        "--gpus-per-socket=2 --sockets-per-node=1 -N1",
    ],
)
def test_enforce_links_two_gpu_gets_linked_pair(gpu_flag):
    """A 2-GPU request in any form must get the linked pair.

    node3, not node1: only {2,3} are linked there. On node1 both pairs are
    linked, so the default preference alone would satisfy the assertion.
    """
    output = atf.run_command_output(
        f"srun --gres-flags=enforce-links {gpu_flag} -w node3 "
        "scontrol show node node3 -d",
        fatal=True,
    )
    indexes = _allocated_gpu_indexes(output)
    assert indexes == {
        2,
        3,
    }, f"enforce-links {gpu_flag} got GPUs {sorted(indexes)}, not the linked pair"


def test_non_enforce_two_gpu_runs_on_node3():
    """Counter to the case above: without the flag a 2-GPU job on node3 still
    runs (the link requirement is what constrains placement, not GPU supply)."""
    output = atf.run_command_output(
        "srun --gpus-per-node=2 -w node3 scontrol show node node3 -d",
        fatal=True,
    )
    assert (
        len(_allocated_gpu_indexes(output)) == 2
    ), "a non-enforce 2-GPU job must run on node3"


def test_enforce_links_single_gpu_succeeds():
    """A single device is trivially linked to itself, so a 1-GPU enforce-links
    request runs normally."""
    job_id = atf.submit_job_sbatch(
        '--gres-flags=enforce-links --gpus-per-node=1 -w node1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "single-GPU enforce-links job should run"


def test_enforce_links_skips_unsatisfiable_node():
    """With no node named, the job must land on node1 or node3 and skip node2,
    which has no link data."""
    job_id = atf.submit_job_sbatch(
        '--gres-flags=enforce-links --gpus-per-node=2 -N1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "enforce-links 2-GPU job should run on a node with a linked pair"
    assert (
        atf.get_job_parameter(job_id, "NodeList") != "node2"
    ), "enforce-links must skip node2, which has no link data"


def test_no_flag_allows_unlinked_gpus():
    """Counter-example: without the flag the same request runs on node2's
    unlinked GPUs, so the pends below are the flag and not a GPU shortage."""
    job_id = atf.submit_job_sbatch(
        '--gpus-per-node=2 -w node2 --wrap "sleep infinity"', fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "without enforce-links, a 2-GPU job must run on node2's unlinked GPUs"


def test_enforce_links_pends_when_linked_pair_busy():
    """With node3's pair {2,3} busy the job pends rather than taking the idle
    unlinked GPUs 0 and 1.

    Freeing the pair must start that same job, which shows the pend was the
    link requirement and not a slow scheduling pass.
    """
    holder = atf.submit_job_sbatch(
        "--gres-flags=enforce-links --gpus-per-node=2 -w node3 "
        '--wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        holder, "RUNNING"
    ), "holder should run on the linked pair {2,3}"
    assert _node_used_gpus("node3") == {
        2,
        3,
    }, "the holder should be on node3's only linked pair"

    probe = atf.submit_job_sbatch(
        "--gres-flags=enforce-links --gpus-per-node=2 -w node3 "
        '--wrap "sleep infinity"',
        fatal=True,
    )
    # Short timeout on purpose: this is a negative wait, so the default 45s
    # would only slow the test down.
    assert not atf.wait_for_job_state(
        probe, "RUNNING", timeout=15
    ), "enforce-links must not use idle unlinked GPUs while {2,3} is busy"
    assert (
        atf.get_job_parameter(probe, "JobState") == "PENDING"
    ), "enforce-links job should pend while the linked pair is busy"
    assert (
        atf.get_job_parameter(probe, "Reason") == "Resources"
    ), "the enforce-links job should be waiting on resources"

    atf.run_command(f"scancel {holder}", fatal=True)
    assert atf.wait_for_job_state(
        probe, "RUNNING", fatal=True
    ), "the pending job should start once the linked pair is freed"
    assert _job_gpu_indexes(probe) == {
        2,
        3,
    }, "the job that was pending should take the linked pair"


def test_non_enforce_uses_idle_unlinked_gpus_when_pair_busy():
    """Counter to the case above: with node3's linked pair busy, a non-enforce
    2-GPU job runs on the idle unlinked GPUs instead of pending."""
    holder = atf.submit_job_sbatch(
        "--gres-flags=enforce-links --gpus-per-node=2 -w node3 "
        '--wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(holder, "RUNNING"), "holder should run on {2,3}"
    probe = atf.submit_job_sbatch(
        '--gpus-per-node=2 -w node3 --wrap "sleep infinity"', fatal=True
    )
    assert atf.wait_for_job_state(
        probe, "RUNNING"
    ), "a non-enforce job should run on node3's idle unlinked GPUs"


def test_enforce_links_no_large_enough_set_rejected():
    """No node has three mutually linked GPUs, so the request is rejected at
    submission."""
    result = atf.run_command(
        "sbatch --gres-flags=enforce-links --gpus-per-node=3 -N1 "
        '--wrap "sleep infinity"',
        xfail=True,
    )
    assert result["exit_code"] != 0, "3-GPU enforce-links must be rejected"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"expected a node-config rejection, got: {result['stderr']}"


def test_non_enforce_three_gpu_runs_on_node3():
    """Counter to the case above: without the flag a 3-GPU request on node3
    runs, taking the linked pair plus an unlinked GPU."""
    job_id = atf.submit_job_sbatch(
        '--gpus-per-node=3 -w node3 --wrap "sleep infinity"', fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "a non-enforce 3-GPU job must run on node3"


def test_enforce_links_request_exceeds_largest_clique_rejected():
    """node3's largest linked set is the pair {2,3}, so a 3-GPU enforce-links
    request pinned there cannot be satisfied and is rejected at submission."""
    result = atf.run_command(
        "sbatch --gres-flags=enforce-links --gpus-per-node=3 -w node3 "
        '--wrap "sleep infinity"',
        xfail=True,
    )
    assert result["exit_code"] != 0, "3-GPU enforce-links on node3 must be rejected"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"expected a node-config rejection, got: {result['stderr']}"


def test_enforce_links_no_link_data_rejected():
    """node2 has no Links, so no node can satisfy a 2-GPU enforce-links request
    pinned there, and it is rejected at submission (fail-closed)."""
    result = atf.run_command(
        "sbatch --gres-flags=enforce-links --gpus-per-node=2 -w node2 "
        '--wrap "sleep infinity"',
        xfail=True,
    )
    assert result["exit_code"] != 0, "enforce-links on a no-link node must be rejected"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"expected a node-config rejection, got: {result['stderr']}"


def test_enforce_links_single_gpu_ok_without_links():
    """A single device needs no link information, so a 1-GPU enforce-links
    request runs even on node2, which has no Links configured."""
    job_id = atf.submit_job_sbatch(
        '--gres-flags=enforce-links --gpus-per-node=1 -w node2 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "single-GPU enforce-links must run even with no link data"


def test_enforce_links_typed_request_scoped_to_type():
    """A typed request is held against the devices of that type alone. node4's
    two a100s ({0,1}) are not linked, so --gres=gpu:a100:2 cannot be satisfied
    anywhere and is rejected at submission."""
    result = atf.run_command(
        "sbatch --gres-flags=enforce-links --gres=gpu:a100:2 -w node4 " '--wrap "true"',
        xfail=True,
    )
    assert result["exit_code"] != 0, "typed enforce-links must be rejected"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"expected a node-config rejection, got: {result['stderr']}"


def test_non_enforce_typed_request_runs_on_node4():
    """Counter to the case above: without the flag the same typed request runs
    on node4's unlinked a100 pair."""
    job_id = atf.submit_job_sbatch(
        '--gres=gpu:a100:2 -w node4 --wrap "sleep infinity"', fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "a non-enforce typed 2-GPU job must run on node4"
    assert _job_gpu_indexes(job_id) == {
        0,
        1,
    }, "the a100 pair is node4's devices 0 and 1"


def test_enforce_links_untyped_uses_global_link_positions():
    """An untyped request is held against every device of the GRES name, and
    the Links rows are node-global: node4's only linked pair is {1,3}, across
    the two Types, so --gres=gpu:2 must get exactly that pair. Reading the
    matrix type-locally would take indexes 0 and 1 instead."""
    output = atf.run_command_output(
        "srun --gres-flags=enforce-links --gres=gpu:2 -w node4 "
        "scontrol show node node4 -d",
        fatal=True,
    )
    indexes = _allocated_gpu_indexes(output)
    assert indexes == {
        1,
        3,
    }, f"enforce-links --gres=gpu:2 got GPUs {sorted(indexes)}, not the linked pair {{1,3}}"


def test_enforce_links_exempts_shared_gres():
    """Shared GRES are slices of one device and have no links, so the flag does
    not apply. A multi-shard request on node2 must still run with it set."""
    job_id = atf.submit_job_sbatch(
        '--gres-flags=enforce-links --gres=shard:4 -w node2 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "enforce-links must not block a shared-GRES (shard) request"


def test_enforce_links_pends_on_freed_cross_pair():
    """With only the idle cross pair {0,2} left on node1, the job must pend
    rather than take two GPUs that are not linked."""
    holder = _leave_idle_cross_pair_on_node1()
    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-links --gpus=2 -N1 -w node1 " '--wrap "sleep infinity"',
        fatal=True,
    )
    # Short timeout on purpose: this is a negative wait, so the default 45s
    # would only slow the test down.
    if atf.wait_for_job_state(job_id, "RUNNING", timeout=15):
        assert False, (
            "enforce-links must not run on the idle cross pair {0,2}, "
            f"but it ran on GPUs {sorted(_job_gpu_indexes(job_id))}"
        )
    assert (
        atf.get_job_parameter(job_id, "JobState") == "PENDING"
    ), "enforce-links job should pend while only a cross pair is idle"
    assert (
        atf.get_job_parameter(job_id, "Reason") == "Resources"
    ), "the enforce-links job should be waiting on resources"

    # Freeing GPU 3 makes {2,3} idle and linked. The same job then has to run,
    # which is what shows it was held back by the links and not by the
    # scheduler taking its time.
    atf.run_command(f"scancel {holder[3]}", fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), "the pending job should start once a linked pair is freed"
    assert _job_gpu_indexes(job_id) == {
        2,
        3,
    }, "the job that was pending should take the linked pair"


def test_non_enforce_runs_on_freed_cross_pair():
    """Counter to the case above: with the same idle cross pair {0,2}, a
    non-enforce 2-GPU job runs on it."""
    _leave_idle_cross_pair_on_node1()
    job_id = atf.submit_job_sbatch(
        '--gpus=2 -N1 -w node1 --wrap "sleep infinity"', fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "a non-enforce 2-GPU job must run on the idle cross pair {0,2}"


def test_enforce_links_node_level_4gpu_rejected():
    """A node-level 4-GPU enforce-links request needs a 4-clique, which node1
    (two linked pairs) cannot form, so it is rejected at submission."""
    result = atf.run_command(
        "sbatch --gres-flags=enforce-links --gpus-per-node=4 -w node1 " '--wrap "true"',
        xfail=True,
    )
    assert result["exit_code"] != 0, "node-level 4-GPU enforce-links must reject"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"expected a node-config rejection, got: {result['stderr']}"


def test_enforce_links_multitask_4gpu_rejected():
    """Two tasks x 2 GPUs needs all four linked on node1, which has no such set,
    so it is rejected. Feasibility sizes the set from the node total, not one
    task's worth."""
    result = atf.run_command(
        "sbatch --gres-flags=enforce-links --ntasks=2 --gpus-per-task=2 -N1 "
        '-w node1 --wrap "true"',
        xfail=True,
    )
    assert result["exit_code"] != 0, "multi-task 4-GPU enforce-links must reject"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"expected a node-config rejection, got: {result['stderr']}"


def test_non_enforce_multitask_4gpu_runs():
    """Counter to the case above: without the flag the same 2-task/4-GPU request
    runs on node1 (the four GPUs need not be linked)."""
    job_id = atf.submit_job_sbatch(
        '--ntasks=2 --gpus-per-task=2 -N1 -w node1 --wrap "sleep infinity"', fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "a non-enforce 2-task/4-GPU job must run on node1"


def test_enforce_links_per_job_spreads_over_nodes():
    """A per-job request names no per-node count, so node selection adds up what
    each node offers. A node can only hand over one linked set, so it must
    offer that rather than every GPU it has.

    node1 and node3 offer a pair each, so 4 GPUs needs both. Advertising all
    four would give the job node1 alone and fail selection every cycle.
    node2 is excluded: with no link data its count would depend on the tie
    break. node4 is excluded: it offers a pair too, which would leave the
    assertions below guessing which two nodes the job took.
    """
    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-links --gpus=4 --exclude=node2,node4 "
        '--wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "a 4-GPU enforce-links job should run across node1 and node3"
    assert (
        atf.get_job_parameter(job_id, "NumNodes") == 2
    ), "the job should be spread over both usable nodes"
    assert _node_used_gpus("node1") in (
        {0, 1},
        {2, 3},
    ), "node1 should give a linked pair"
    assert _node_used_gpus("node3") == {
        2,
        3,
    }, "node3 should give its only linked pair"


def test_enforce_links_per_job_not_satisfiable_rejected():
    """Counter to the case above: node1 and node3 offer a pair each, so six GPUs
    cannot be found however the job is spread (node4's pair would make it
    six, so it is excluded too)."""
    result = atf.run_command(
        "sbatch --gres-flags=enforce-links --gpus=6 --exclude=node2,node4 "
        '--wrap "sleep infinity"',
        xfail=True,
    )
    assert result["exit_code"] != 0, f"should be rejected, got {result}"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"should be refused for the node configuration, got {result}"


def test_non_enforce_per_job_fits_on_one_node():
    """Counter to the two cases above: without the flag the GPUs need not be
    linked, so four GPUs fit on a single node and six are available."""
    job_id = atf.submit_job_sbatch(
        '--gpus=4 --exclude=node2,node4 --wrap "sleep infinity"', fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "a non-enforce 4-GPU job should run"
    assert (
        atf.get_job_parameter(job_id, "NumNodes") == 1
    ), "without the flag all four GPUs fit on one node"


def test_enforce_links_reported_by_scontrol_and_json():
    """The flag is echoed back as GresEnforceLinks, and as GRES_LINKS_ENFORCED
    in the data parser JSON."""
    job_id = atf.submit_job_sbatch(
        '--gres-flags=enforce-links --gpus-per-node=1 -w node1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(job_id, "RUNNING"), "the job should run"
    assert (
        atf.get_job_parameter(job_id, "GresEnforceLinks") == "Yes"
    ), "scontrol should report the enforce-links flag"
    flags = atf.get_jobs(job_id, use_json=True)[job_id]["flags"]
    assert (
        "GRES_LINKS_ENFORCED" in flags
    ), f"the JSON job flags should name enforce-links, got {flags}"
