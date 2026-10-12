############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify --gres-flags=enforce-links where a node's request gets split.

The link requirement covers everything a job gets on a node, however the
selection code divides it up and however the request is spelled.

  - By socket. A job whose cores span both sockets has its per-node request
    divided between them, down to a single device per part. The requirement is
    still decided against the whole allocation: per part, the first device
    would be picked blind to the links and bind every later pick.

  - By socket, where the socket also binds. A per-socket request names how many
    devices of each socket the job gets, so a call may only hand over that
    socket's devices. The set they come from is still the node's and may reach
    into the others, which is where the calls that follow take theirs.

  - By spelling. --gpus=N -N1 asks the same as --gpus-per-node=N -N1, so both
    must be refused and accepted alike. Both are pinned to one node: spread
    over several, a node holding a single device is exempt.

Each case is paired with a counter-example that still runs, or is still
refused, without the flag.

Three nodes, two sockets and four GPUs each:
  - node1: GPUs 0,1 on socket 0 and 2,3 on socket 1, linked in those pairs
  - node2: no Links configured
  - node3: linked only as {1,3}, one device per socket. GPU 0 comes first on
    socket 0 and is linked to nothing, so a pick going by index order, or
    looking for the node's set within socket 0 alone, would take it and lose
    the node.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

SOCKETS = 2
CORES_PER_SOCKET = 4
GPUS = 4


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
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
    atf.require_config_parameter("SelectTypeParameters", "CR_CORE")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(
        3,
        [
            ("Gres", f"gpu:{GPUS}"),
            ("Sockets", SOCKETS),
            ("CoresPerSocket", CORES_PER_SOCKET),
            ("ThreadsPerCore", 1),
        ],
    )

    gpu_file = f"{atf.module_tmp_path}/gpu"
    for i in range(GPUS):
        atf.run_command(f"touch {gpu_file}{i}", fatal=True)

    # node1: GPUs 0,1 on socket 0 and GPUs 2,3 on socket 1, each pair linked to
    # itself only, so a linked pair never spans a socket.
    # node2: the same GPUs with no Links, i.e. no link information at all.
    # node3: the same GPUs linked only as {1,3}, so the only linked pair holds
    # one device of each socket, and it is not the first device of either.
    atf.require_config_parameter(
        "NodeName",
        f"""node1 Name=gpu File={gpu_file}0 Cores=0-3 Links=-1,1,0,0
        NodeName=node1 Name=gpu File={gpu_file}1 Cores=0-3 Links=1,-1,0,0
        NodeName=node1 Name=gpu File={gpu_file}2 Cores=4-7 Links=0,0,-1,1
        NodeName=node1 Name=gpu File={gpu_file}3 Cores=4-7 Links=0,0,1,-1
        NodeName=node2 Name=gpu File={gpu_file}0 Cores=0-3
        NodeName=node2 Name=gpu File={gpu_file}1 Cores=0-3
        NodeName=node2 Name=gpu File={gpu_file}2 Cores=4-7
        NodeName=node2 Name=gpu File={gpu_file}3 Cores=4-7
        NodeName=node3 Name=gpu File={gpu_file}0 Cores=0-3 Links=-1,0,0,0
        NodeName=node3 Name=gpu File={gpu_file}1 Cores=0-3 Links=0,-1,0,1
        NodeName=node3 Name=gpu File={gpu_file}2 Cores=4-7 Links=0,0,-1,0
        NodeName=node3 Name=gpu File={gpu_file}3 Cores=4-7 Links=0,1,0,-1""",
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


def _job_gpu_indexes(job_id):
    """Return the set of GPU indexes allocated to a job on its first node."""
    out = atf.run_command_output(f"scontrol show job {job_id} -d", fatal=True)
    match = re.search(r"GRES=gpu:[^(]*\(IDX:([0-9,\-]+)\)", out)
    assert match is not None, f"No GPU IDX for job {job_id}:\n{out}"
    return _parse_idx(match.group(1))


def _job_gpus_by_node(job_id):
    """{node name: set of GPU indexes} for a job spanning several nodes.

    One allocation line per group of nodes given the same resources. Indexes
    are per node, so the total is the sum of the sizes, not their union.
    """
    out = atf.run_command_output(f"scontrol show job {job_id} -d", fatal=True)
    matches = re.findall(
        r"Nodes=(\S+) CPU_IDs=\S+ Mem=\S+ GRES=gpu:[^(]*\(IDX:([0-9,\-]+)\)", out
    )
    assert matches, f"No GPU IDX for job {job_id}:\n{out}"
    return {node: _parse_idx(idx) for node, idx in matches}


def _job_gpu_count(job_id):
    """Return how many GPUs a job holds across all of its nodes."""
    return sum(len(idx) for idx in _job_gpus_by_node(job_id).values())


def _node_used_gpus(node):
    """Return the set of GPU indexes in use on a node, empty when none are."""
    out = atf.run_command_output(f"scontrol show node {node} -d", fatal=True)
    match = re.search(r"GresUsed=gpu:\d+\(IDX:([0-9,\-]+)\)", out)
    if not match:
        return set()
    return _parse_idx(match.group(1))


def _hold_every_gpu(node):
    """Occupy each of the node's GPUs with a one-GPU job.

    Returns {gpu: job id}. The GPU a holder lands on is read back, since the
    link-count sort does not fix the order.
    """
    holder = {}
    for _ in range(GPUS):
        job_id = atf.submit_job_sbatch(
            f'--gpus-per-node=1 -w {node} --wrap "sleep infinity"', fatal=True
        )
        assert atf.wait_for_job_state(
            job_id, "RUNNING", fatal=True
        ), f"holder job {job_id} should run"
        indexes = _job_gpu_indexes(job_id)
        assert len(indexes) == 1, f"holder {job_id} took GPUs {sorted(indexes)}"
        holder[next(iter(indexes))] = job_id
    assert set(holder) == set(
        range(GPUS)
    ), f"every GPU should be held, got {sorted(holder)}"
    return holder


def _free_all_gpus_but(node, holder, keep):
    """Cancel every holder but the one on 'keep' and wait for the release.

    JobState reads CANCELLED while the GRES are still allocated, so wait on
    the node's own used-GPU set.
    """
    for gpu, job_id in holder.items():
        if gpu != keep:
            atf.run_command(f"scancel {job_id}", fatal=True)
    for _ in atf.timer(fatal=True):
        if _node_used_gpus(node) == {keep}:
            break


def test_enforce_links_spans_sockets_when_first_socket_unlinked():
    """With GPU 1 busy the only pair left is {2,3}, so a job whose cores span
    both sockets must get it. Picking GPU 0 first, whose partner is busy,
    would leave nothing to link to and lose the node."""
    holder = _hold_every_gpu("node1")
    _free_all_gpus_but("node1", holder, keep=1)

    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-links --gpus-per-node=2 --ntasks=2 "
        '--ntasks-per-socket=1 -w node1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "enforce-links job should run on the linked pair left on socket 1"
    assert _job_gpu_indexes(job_id) == {
        2,
        3,
    }, "enforce-links must select the linked pair {2,3}"


def test_no_flag_spans_sockets_when_first_socket_unlinked():
    """Control: the same request without the flag runs, so a pend is the link
    requirement and not a shortage of GPUs or cores."""
    holder = _hold_every_gpu("node1")
    _free_all_gpus_but("node1", holder, keep=1)

    job_id = atf.submit_job_sbatch(
        "--gpus-per-node=2 --ntasks=2 --ntasks-per-socket=1 -w node1 "
        '--wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "a job without enforce-links should run on the free GPUs"
    assert (
        len(_job_gpu_indexes(job_id)) == 2
    ), "a job without enforce-links should get two GPUs"


def test_enforce_links_per_socket_takes_the_cross_socket_pair():
    """Each pick is bound to its socket, but the set it draws from is the node's.

    node3's only pair is {1,3}, one per socket, and GPU 0 comes first on
    socket 0 linked to nothing. The pick for socket 0 must hand over GPU 1.
    """
    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-links --gpus-per-socket=1 --sockets-per-node=2 "
        '--ntasks=2 --ntasks-per-socket=1 -w node3 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "enforce-links should run on node3's cross-socket linked pair"
    assert _job_gpu_indexes(job_id) == {
        1,
        3,
    }, "enforce-links must select the linked pair {1,3}"


def test_no_flag_per_socket_runs_on_node3():
    """Control: the same request without the flag runs, so a pend is the link
    requirement and not a shortage of GPUs or cores."""
    job_id = atf.submit_job_sbatch(
        "--gpus-per-socket=1 --sockets-per-node=2 --ntasks=2 "
        '--ntasks-per-socket=1 -w node3 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "a job without enforce-links should run on node3"
    assert (
        len(_job_gpu_indexes(job_id)) == 2
    ), "a job without enforce-links should get one GPU on each socket"


def test_enforce_links_per_socket_never_yields_an_unlinked_pair():
    """node1's pairs each sit inside one socket, so no cross-socket pair is
    linked. A per-socket request over both cannot have both the count and the
    links, and the links hold: the job must not get one device per socket."""
    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-links --gpus-per-socket=1 --sockets-per-node=2 "
        '--ntasks=2 --ntasks-per-socket=1 -w node1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "the per-socket count should give way to a linked set, so the job runs"

    assert _job_gpu_indexes(job_id) in (
        {0, 1},
        {2, 3},
    ), "enforce-links must give a linked pair, even at the cost of the count"


def test_no_flag_per_socket_runs_on_node1():
    """Control for the case above. The identical request without the flag runs
    on node1, so the pend is caused by the link requirement and not by the
    per-socket count or by a shortage of cores."""
    job_id = atf.submit_job_sbatch(
        "--gpus-per-socket=1 --sockets-per-node=2 --ntasks=2 "
        '--ntasks-per-socket=1 -w node1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "a job without enforce-links should run on node1"
    assert (
        len(_job_gpu_indexes(job_id)) == 2
    ), "a job without enforce-links should get one GPU on each socket"


def test_gpus_flag_spread_when_unlinked_node_comes_first():
    """A per-job request names no count per node, so a node's share is the
    leftover once the nodes still to come are set aside.

    A node given a single device is exempt, and node2 can only ever be given
    one. Its share has to be held to what it can supply for that to be seen:
    the plain leftover reads as two, and node2 then loses the exemption. Only
    the last node is safe from that, so node2 is deliberately the earlier.
    """
    job_id = atf.submit_job_sbatch(
        "--gres-flags=enforce-links --gpus=3 -N2 -w node2,node3 "
        '--wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "enforce-links should spread a 3-GPU request over node2 and node3"

    by_node = _job_gpus_by_node(job_id)
    assert _job_gpu_count(job_id) == 3, f"the job should hold three GPUs, got {by_node}"
    assert by_node.get("node3") == {
        1,
        3,
    }, f"node3 should supply its linked pair, got {by_node}"
    assert (
        len(by_node.get("node2", set())) == 1
    ), f"node2 should supply the single device it is exempt for, got {by_node}"


def test_no_flag_spread_when_unlinked_node_comes_first():
    """Control: the same request without the flag runs, so a pend is the link
    requirement and not a shortage of GPUs across the two nodes."""
    job_id = atf.submit_job_sbatch(
        '--gpus=3 -N2 -w node2,node3 --wrap "sleep infinity"', fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "a 3-GPU request over two nodes should run without the flag"
    assert (
        _job_gpu_count(job_id) == 3
    ), "a job without enforce-links should get three GPUs"


def test_gpus_per_node_rejected_without_links():
    """Control: node2 has no link information, so --gpus-per-node=2 with
    enforce-links is refused at submission."""
    result = atf.run_command(
        "sbatch --gres-flags=enforce-links --gpus-per-node=2 -N1 -w node2 "
        '--wrap "true"',
        xfail=True,
    )
    assert result["exit_code"] != 0, f"should be rejected, got {result}"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"should be rejected for the node configuration, got {result}"


def test_gpus_flag_rejected_without_links():
    """--gpus=2 -N1 asks for the same thing as --gpus-per-node=2 -N1, so it
    must be refused on node2 for the same reason."""
    result = atf.run_command(
        'sbatch --gres-flags=enforce-links --gpus=2 -N1 -w node2 --wrap "true"',
        xfail=True,
    )
    assert result["exit_code"] != 0, f"should be rejected, got {result}"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"should be rejected for the node configuration, got {result}"


def test_gpus_per_node_rejected_when_linked_set_too_small():
    """Control: node1's largest linked set is a pair, so a three GPU
    enforce-links request is refused at submission."""
    result = atf.run_command(
        "sbatch --gres-flags=enforce-links --gpus-per-node=3 -N1 -w node1 "
        '--wrap "true"',
        xfail=True,
    )
    assert result["exit_code"] != 0, f"should be rejected, got {result}"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"should be rejected for the node configuration, got {result}"


def test_gpus_flag_rejected_when_linked_set_too_small():
    """--gpus=3 -N1 must be refused on node1 for the same reason as
    --gpus-per-node=3 -N1: no three of its GPUs are mutually linked."""
    result = atf.run_command(
        'sbatch --gres-flags=enforce-links --gpus=3 -N1 -w node1 --wrap "true"',
        xfail=True,
    )
    assert result["exit_code"] != 0, f"should be rejected, got {result}"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), f"should be rejected for the node configuration, got {result}"
