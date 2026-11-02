############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""--exclusive=topo on a partition whose topology plugin does not enforce it.

select/cons_tres keeps other jobs off the whole topology unit of a running
--exclusive=topo job, and simulates such a job ending over its whole unit,
only where the topology plugin of the job's partition enforces exclusive
units, as topology/block does and topology/tree does not. With a block and
a tree topology configured together, an --exclusive=topo job holding a GRES
runs on one node of a block partition and of a tree partition: another job
must run next to it on the tree partition's leaf switch, planning a third
job there must leave the GRES in use alone and log no cons_tres error, and
the same pair on the block partition must stay apart until the exclusive
job ends, also when the second job comes through the tree partition, which
holds the nodes of both units: the partition of the running job decides
what it holds.

Backfill reserves the whole unit of an --exclusive=topo job whatever the
plugin of its partition, so the planned start time of a job next to it is
not checked here.
"""

import re

import pytest

import atf

_UNIT_SIZE = 4
_BLOCK_PARTITION = "test_150_21_block"
_TREE_PARTITION = "test_150_21_tree"
_GRES = "r1"
_ERROR_PATTERN = "under-allocated|mis-count"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 25865: exclusive topology per partition plugin since 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_config_parameter_includes("GresTypes", _GRES)
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    # bf_min_prio_reserve keeps backfill from planning the jobs below its
    # priority threshold, and planning next to the exclusive job is what is
    # checked here.
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_min_prio_reserve", 0)
    )

    # require_nodes() can return qualifying nodes of the base configuration
    # before the ones it creates; the units are made of the created ones.
    nodes = atf.require_nodes(
        2 * _UNIT_SIZE, [("CPUs", 2), ("RealMemory", 512), ("Gres", f"{_GRES}:1")]
    )
    block_unit = nodes[-2 * _UNIT_SIZE : -_UNIT_SIZE]
    tree_unit = nodes[-_UNIT_SIZE:]
    block_nodes = ",".join(block_unit)
    tree_nodes = ",".join(tree_unit)
    # Every node is a member of both topologies: a block of the block
    # topology and a leaf switch of the tree topology hold the same nodes.
    atf.require_config_file(
        "topology.yaml",
        f"""
- topology: block_topo
  cluster_default: true
  block:
    block_sizes:
      - {_UNIT_SIZE}
    blocks:
      - block: b1
        nodes: "{block_nodes}"
      - block: b2
        nodes: "{tree_nodes}"
- topology: tree_topo
  cluster_default: false
  tree:
    switches:
      - switch: root
        children: s[1-2]
      - switch: s1
        nodes: "{block_nodes}"
      - switch: s2
        nodes: "{tree_nodes}"
""",
    )
    # The tree partition holds the nodes of both units, so a job through it
    # can be sent next to an --exclusive=topo job of the block partition.
    atf.require_config_parameter(
        "PartitionName",
        {
            _BLOCK_PARTITION: {"Nodes": block_nodes, "Topology": "block_topo"},
            _TREE_PARTITION: {
                "Nodes": f"{block_nodes},{tree_nodes}",
                "Topology": "tree_topo",
            },
        },
    )
    atf.require_slurm_running()

    return block_unit, tree_unit


def _slurmctld_log():
    """The slurmctld log path, case preserved and checked readable.

    get_config_parameter() casefolds the value it returns, so it can not be
    used for a path; get_config() preserves case.
    """

    config = atf.get_config(live=False, quiet=True)
    path = next(
        (
            value
            for key, value in config.items()
            if key.casefold() == "slurmctldlogfile"
        ),
        None,
    )
    assert path, "SlurmctldLogFile must be set to check the slurmctld log"
    atf.run_command(
        f'test -r "{path}"',
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    return path


def _log_mark(log):
    """Current line count of the slurmctld log, to read forward from later."""

    return int(
        atf.run_command_output(
            f"wc -l < {log}",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        ).strip()
    )


def _gres_used(node):
    output = atf.run_command_output(f"scontrol show node {node} -d", fatal=True)
    match = re.search(r"GresUsed=(\S+)", output)
    assert match, f"No GresUsed in the details of node {node}:\n{output}"
    return match.group(1)


def _submit(args):
    return atf.submit_job_sbatch(
        f"-N1 -o /dev/null {args} --wrap 'sleep infinity'", fatal=True
    )


def test_tree_partition_does_not_enforce_exclusive_topo(setup):
    """Jobs run and are planned next to an --exclusive=topo job on tree."""

    _, (node_a, node_b, *_) = setup
    exclusive = _submit(
        f"-p {_TREE_PARTITION} -w {node_a} --exclusive=topo --gres={_GRES}:1 -t5"
    )
    atf.wait_for_job_state(exclusive, "RUNNING", fatal=True)

    mate = _submit(f"-p {_TREE_PARTITION} -w {node_b} -t5")
    assert atf.wait_for_job_state(mate, "RUNNING"), (
        f"Job ({mate}) should run on {node_b} next to the --exclusive=topo job "
        f"{exclusive}: topology/tree does not enforce exclusive units"
    )

    # The mate holds a CPU of its node, so a whole-node job for it is
    # planned by backfill instead of started.
    log = _slurmctld_log()
    mark = _log_mark(log)
    planned = _submit(f"-p {_TREE_PARTITION} -w {node_b} --exclusive -t1")
    for _ in atf.timer(fatal=True):
        start = atf.get_job_parameter(
            planned, "StartTime", default="Unknown", quiet=True
        )
        if start != "Unknown":
            break
    state = atf.get_job_parameter(planned, "JobState", quiet=True)
    assert (
        state == "PENDING"
    ), f"Job ({planned}) should wait for job {mate} on {node_b}, is {state}"

    gres_used = _gres_used(node_a)
    assert (
        gres_used == f"{_GRES}:1"
    ), f"Planning job {planned} changed the GRES in use on {node_a} to {gres_used}"
    errors = atf.run_command_output(
        f"tail -n +{mark + 1} {log} | grep -E '{_ERROR_PATTERN}' || true",
        user=atf.properties["slurm-user"],
        quiet=True,
    ).strip()
    assert (
        not errors
    ), f"slurmctld logged errors while planning job {planned}:\n{errors}"

    atf.cancel_jobs([planned, mate, exclusive], fatal=True)


@pytest.mark.parametrize(
    "pending_partition",
    [_BLOCK_PARTITION, _TREE_PARTITION],
    ids=["same_partition", "tree_partition_sharing_the_block"],
)
def test_block_partition_enforces_exclusive_topo(setup, pending_partition):
    """A job for another node of the block waits for the --exclusive=topo job.

    The partition of the running job decides what it holds, so a job sent
    through the tree partition to a node of the block waits too.
    """

    (node_a, node_b, *_), _ = setup
    exclusive = _submit(
        f"-p {_BLOCK_PARTITION} -w {node_a} --exclusive=topo --gres={_GRES}:1 -t5"
    )
    atf.wait_for_job_state(exclusive, "RUNNING", fatal=True)

    job = _submit(f"-p {pending_partition} -w {node_b} -t1")
    assert atf.wait_for_job_state(job, "PENDING", desired_reason="Resources"), (
        f"Job ({job}) should wait for the --exclusive=topo job {exclusive} "
        f"holding the block of {node_b}: topology/block enforces exclusive units"
    )

    atf.cancel_jobs([exclusive], fatal=True)
    assert atf.wait_for_job_state(
        job, "RUNNING"
    ), f"Job ({job}) never started once job {exclusive} was gone"
    allocated = atf.node_range_to_list(
        atf.get_job_parameter(job, "NodeList", quiet=True)
    )
    assert allocated == [node_b], f"Job ({job}) should run on {node_b}, got {allocated}"

    atf.cancel_jobs([job], fatal=True)
