############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Backfill planning of a node list job next to an --exclusive=topo job.

A job whose required node list is as long as its fixed node count is only
evaluated on those nodes. select/cons_tres simulates a running
--exclusive=topo job ending over the whole topology unit it holds, so it has
to copy the partitions and the node usage of the whole units of the nodes it
is handed, not of those nodes alone. With copies of the required nodes alone,
the simulation reaches nodes it has no copy of: it logs "memory is
under-allocated" and "node_state mis-count" errors on every backfill cycle,
deallocates the exclusive job's GRES from the live node, and does not see an
exclusive job in a partition holding only other nodes of the unit at all, so
the waiting job is planned for now instead of after it.

An --exclusive=topo job holding a GRES runs on one node of a block while a
-N1 -w job for another node of the block waits behind it, with the exclusive
job in the same partition and in a partition holding only its half of the
block. Backfill must plan the waiting job for the end of the exclusive job
without logging those errors and without touching the GRES in use, and the
job must start on its node once the exclusive job is gone.
"""

import datetime
import re

import pytest

import atf

_BLOCK_SIZE = 4
_PARTITION = "test_150_20"
_HALF_PARTITION = "test_150_20_half"
_GRES = "r1"
_ERROR_PATTERN = "under-allocated|mis-count"
# Backfill rounds a start time estimate up to the next bf_resolution boundary
_BF_RESOLUTION_SEC = 60
# The times are parsed, so scontrol has to print them in the standard format
_TIME_FORMAT = "SLURM_TIME_FORMAT=standard"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 25865: select/cons_tres copies whole topology units since 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_config_parameter_includes("GresTypes", _GRES)
    atf.require_config_parameter("TopologyPlugin", "topology/block")
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_resolution", _BF_RESOLUTION_SEC)
    )
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    # bf_min_prio_reserve keeps backfill from planning the jobs below its
    # priority threshold, and the planned start time is what is checked here.
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_min_prio_reserve", 0)
    )

    # require_nodes() can return qualifying nodes of the base configuration
    # before the ones it creates; the block is made of the created ones.
    nodes = atf.require_nodes(
        _BLOCK_SIZE, [("CPUs", 2), ("RealMemory", 512), ("Gres", f"{_GRES}:1")]
    )
    block = nodes[-_BLOCK_SIZE:]
    atf.require_config_file(
        "topology.conf",
        f"BlockName=b1 Nodes={','.join(block)}\nBlockSizes={_BLOCK_SIZE}\n",
    )
    # The jobs are submitted to these partitions, so nodes outside the block
    # can not take them.
    atf.require_config_parameter(
        "PartitionName",
        {
            _PARTITION: {"Nodes": ",".join(block)},
            _HALF_PARTITION: {"Nodes": ",".join(block[_BLOCK_SIZE // 2 :])},
        },
    )
    atf.require_slurm_running()

    return block


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


@pytest.mark.parametrize(
    "exclusive_partition",
    [_PARTITION, _HALF_PARTITION],
    ids=["same_partition", "partition_cutting_the_block"],
)
def test_node_list_job_planned_after_exclusive_topo_job(setup, exclusive_partition):
    """-N1 -w a waits behind the --exclusive=topo job on c until it ends."""

    node_a, _, node_c, _ = setup
    exclusive = _submit(
        f"-p {exclusive_partition} -w {node_c} --exclusive=topo --gres={_GRES}:1 -t5"
    )
    atf.wait_for_job_state(exclusive, "RUNNING", fatal=True)
    exclusive_end = datetime.datetime.fromisoformat(
        atf.get_job_parameter(exclusive, "EndTime", env_vars=_TIME_FORMAT, quiet=True)
    )

    log = _slurmctld_log()
    mark = _log_mark(log)
    job = _submit(f"-p {_PARTITION} -w {node_a} -t1")
    for _ in atf.timer(fatal=True):
        start = atf.get_job_parameter(
            job, "StartTime", default="Unknown", env_vars=_TIME_FORMAT, quiet=True
        )
        if start != "Unknown":
            break
    state = atf.get_job_parameter(job, "JobState", quiet=True)
    assert (
        state == "PENDING"
    ), f"Job ({job}) should wait for job {exclusive}, is {state}"
    planned = datetime.datetime.fromisoformat(start)
    assert (
        exclusive_end
        <= planned
        <= exclusive_end + datetime.timedelta(seconds=_BF_RESOLUTION_SEC)
    ), f"Job ({job}) planned for {start}, job {exclusive} ends at {exclusive_end}"

    gres_used = _gres_used(node_c)
    assert (
        gres_used == f"{_GRES}:1"
    ), f"Planning job {job} changed the GRES in use on {node_c} to {gres_used}"
    errors = atf.run_command_output(
        f"tail -n +{mark + 1} {log} | grep -E '{_ERROR_PATTERN}' || true",
        user=atf.properties["slurm-user"],
        quiet=True,
    ).strip()
    assert not errors, f"slurmctld logged errors while planning job {job}:\n{errors}"

    atf.cancel_jobs([exclusive], fatal=True)
    assert atf.wait_for_job_state(
        job, "RUNNING"
    ), f"Job ({job}) never started once job {exclusive} was gone"
    allocated = atf.node_range_to_list(
        atf.get_job_parameter(job, "NodeList", quiet=True)
    )
    assert allocated == [node_a], f"Job ({job}) should run on {node_a}, got {allocated}"
