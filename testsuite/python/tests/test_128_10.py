############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""
Verify a job preempted before it runs is reported as PREEMPTED.

A job holds its allocation from the moment it is selected, so a higher priority
job can reclaim it before it ever runs. With PrologFlags=Alloc that window is
the whole Prolog, and the job is CONFIGURING for all of it. The kill path for a
configuring job reported every termination as CANCELLED, which hid preemption as
the reason the job ended.

The Prolog blocks on a sentinel file rather than sleeping for a fixed time, so
the preemptee is still configuring when the preemptor arrives however the two
submissions happen to be scheduled.

The state the job records and the kill RPC its nodes are sent both come from
the same preempt flag, so both are checked. The RPC is read out of the
slurmctld log, since a job with no step running yet is torn down the same way
whichever kill it is handed.
"""

import os

import pytest

import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup(block_file):
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 15001: a preempted CONFIGURING job is PREEMPTED in 26.11",
    )

    # enable_stepmgr is only supported with PrologFlags=contain. slurmctld
    # refuses to start with Alloc under it and keeps its previous flags, which
    # would leave the preemptee RUNNING and test a different window.
    if atf.config_parameter_includes(
        "SlurmctldParameters", "enable_stepmgr", live=False, quiet=True
    ):
        pytest.skip("enable_stepmgr is only supported with PrologFlags=contain")

    prolog_script = f"{os.getcwd()}/prolog.sh"
    atf.make_bash_script(
        prolog_script,
        f"""while [ -f {block_file} ]; do
    sleep 0.5
done
""",
    )

    atf.require_config_parameter("Prolog", prolog_script)
    atf.require_config_parameter("PrologFlags", "Alloc")
    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    atf.require_config_parameter("PreemptMode", "CANCEL")

    # The agent names the RPC it dispatches only under this flag, which is how
    # the kill sent to the nodes is told apart from a plain terminate.
    atf.require_config_parameter_includes("DebugFlags", "Agent")

    # Memory has to be configured for a SelectTypeParameters tracking it.
    atf.require_nodes(1, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def block_file():
    """Return the file every Prolog waits on for as long as it exists.

    The Prolog runs on the slurmd with a working directory of its own, so the
    file is reached by absolute path rather than relative to the test.
    """

    return f"{os.getcwd()}/block_prolog"


@pytest.fixture(scope="module")
def preempt_node():
    """Return the one node both partitions are restricted to.

    Both jobs are exclusive, so they only contend for a node if there is only
    one node they can run on.
    """

    return sorted(atf.get_nodes())[0]


@pytest.fixture(scope="module")
def low_priority_partition(preempt_node):
    """Create the partition the preemptee runs in."""

    partition_name = "lowprio"
    atf.run_command(
        f"scontrol create partitionname={partition_name} nodes={preempt_node} "
        f"priority=1 preemptmode=cancel",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    return partition_name


@pytest.fixture(scope="module")
def high_priority_partition(preempt_node):
    """Create the partition the preemptor runs in."""

    partition_name = "highprio"
    atf.run_command(
        f"scontrol create partitionname={partition_name} nodes={preempt_node} "
        f"priority=2 preemptmode=off",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    return partition_name


def _slurmctld_log():
    return atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)


def log_mark():
    """Current line count of the slurmctld log, to read forward from later.

    Marking a position beats grepping the whole file: the log is not cleared
    between tests in a module, so an earlier kill would otherwise count.
    """

    return int(
        atf.run_command_output(
            f"wc -l < {_slurmctld_log()}",
            user=atf.properties["slurm-user"],
            quiet=True,
        ).strip()
    )


def kill_rpcs_since(mark):
    """The kill RPC names the agent dispatched after mark, in order.

    DebugFlags=Agent makes the agent thread log the message type it is about
    to send, which is the only place the choice deallocate_nodes() made is
    visible from outside: for a job with no step running yet, the node does
    the same work whichever kill RPC it is handed.
    """

    return atf.run_command_output(
        f"tail -n +{mark} {_slurmctld_log()} | "
        f"grep -oE 'REQUEST_(KILL_PREEMPTED|KILL_TIMELIMIT|TERMINATE_JOB)' || true",
        user=atf.properties["slurm-user"],
        quiet=True,
    ).split()


@pytest.fixture
def release_prologs(block_file):
    """Hold every Prolog until the yielded function releases them.

    Released again on the way out so that a failure part way through still
    lets the Prologs exit and leaves the nodes usable for later tests.
    """

    def release():
        atf.run_command(f"rm -f {block_file}", fatal=True)

    atf.run_command(f"touch {block_file}", fatal=True)
    yield release
    release()


def test_preempt_configuring_job(
    release_prologs, low_priority_partition, high_priority_partition
):
    """Verify a job preempted while its Prolog runs is reported as PREEMPTED."""

    preemptee = atf.submit_job_sbatch(
        f"-N1 -t1 -o /dev/null --exclusive -p {low_priority_partition} "
        f"--wrap 'sleep infinity'",
        fatal=True,
    )

    assert atf.wait_for_job_state(
        preemptee, "CONFIGURING", timeout=60
    ), f"Preemptee ({preemptee}) was not held in CONFIGURING by its Prolog"

    # Marked before the preemptor exists, so the only kill that can appear
    # past here is the one preemption drives.
    mark = log_mark()

    atf.submit_job_sbatch(
        f"-N1 -t1 -o /dev/null --exclusive -p {high_priority_partition} "
        f"--wrap 'true'",
        fatal=True,
    )

    # The Prolog is still blocked, so being preempted is the only way the
    # preemptee can leave CONFIGURING.
    for _ in atf.timer(timeout=60):
        state = atf.get_job_parameter(
            preemptee, "JobState", default="NOT_FOUND", quiet=True
        )
        if state != "CONFIGURING":
            break
    else:
        pytest.fail(
            f"Preemptee ({preemptee}) was not preempted while it was CONFIGURING"
        )

    # A job being killed reads as COMPLETING until its Prolog exits and the
    # node can finish cleaning up, which is when the state it ended in
    # becomes visible.
    release_prologs()

    assert atf.wait_for_job_state(preemptee, "PREEMPTED", timeout=60), (
        f"Preemptee ({preemptee}) ended in state "
        f"{atf.get_job_parameter(preemptee, 'JobState', default='NOT_FOUND')} "
        f"rather than PREEMPTED"
    )

    # The state above is what the job recorded; this is what its nodes were
    # told. Both come from the same preempt flag, so a kill that reports
    # PREEMPTED has to tear the job down with the preemption RPC too. Only
    # a preemption sends this one, so finding it is enough -- the preemptor
    # finishing in the same window contributes a terminate of its own.
    kills = kill_rpcs_since(mark)
    assert "REQUEST_KILL_PREEMPTED" in kills, (
        f"Preemptee ({preemptee}) should have had its nodes sent "
        f"REQUEST_KILL_PREEMPTED, but the agent only dispatched "
        f"{sorted(set(kills)) or 'no kill RPC at all'}"
    )
