############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the DBD agent queue drains after a stranded requeue of a resized job.

MR 4087: a resize splits off an accounting record keyed on the resize time.
A requeue whose steps cannot be reaped never finishes, and the job_start the
next accounting re-registration sends for it collided with the existing
records. slurmdbd rejected it with a duplicate key and slurmctld retried it
forever, so sdiag's DBD Agent queue size never went back to zero.
"""

import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5, 5),
        "sbin/slurmctld",
        reason="MR 4087: a requeue stranded after a resize wedged the DBD agent queue",
    )
    atf.require_accounting()
    atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.fixture
def kill_slurmd():
    """Kill a node's slurmd under a job, restarting it at teardown.

    Teardown also waits for the job's steps to finish on the restarted node.
    """

    killed = []

    def kill(node, job_id):
        killed.append((node, job_id))
        atf.run_command(f"pkill -9 -f '[s]lurmd -N {node}$'", user="root", fatal=True)
        for t in atf.timer(fatal=True):
            if atf.run_command_exit(f"pgrep -f '[s]lurmd -N {node}$'", quiet=True):
                break

    yield kill

    for node, job_id in killed:
        atf.start_slurmd(node, quiet=True)
        atf.run_command(
            f"scontrol update nodename={node} state=resume",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        atf.wait_for_node_state(node, "IDLE", fatal=True)

        # The stranded step reports its completion only once its node's
        # slurmd is back, and it can't do that after slurmctld is stopped at
        # module teardown.
        atf.cancel_jobs([job_id], fatal=True)
        for t in atf.timer(fatal=True):
            if atf.run_command_exit(
                f"pgrep -f '[s]lurmstepd: \\[{job_id}\\.'", quiet=True
            ):
                break


def dbd_agent_queue_size():
    """Return slurmctld's count of messages still queued for slurmdbd."""

    output = atf.run_command_output("sdiag", fatal=True)
    match = re.search(r"DBD Agent queue size:\s*(\d+)", output)
    assert match, f"sdiag reported no DBD agent queue size:\n{output}"
    return int(match.group(1))


def wait_for_dbd_agent_drained():
    """Wait until slurmctld has nothing left queued for slurmdbd.

    A message slurmdbd rejects stays queued and is retried, so two empty
    readings in a row are required rather than a momentarily empty queue.
    """

    drained_polls = 0
    for t in atf.timer(fatal=True):
        drained_polls = drained_polls + 1 if dbd_agent_queue_size() == 0 else 0
        if drained_polls == 2:
            break


def test_stranded_requeue_after_resize(kill_slurmd):
    """Verify a requeue stranded after a resize doesn't wedge the DBD agent queue."""

    # The batch script outlives its step so the job survives the node loss.
    atf.make_bash_script(
        "job.sh", "srun --no-kill -N2 -n2 sleep infinity &\nsleep infinity"
    )
    job_id = atf.submit_job_sbatch("-N2 --no-kill --requeue job.sh", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.wait_for_step(job_id, 0, fatal=True)

    nodes = atf.run_command_output(
        f"scontrol show hostnames $(squeue -h -j {job_id} -o %N)", fatal=True
    ).split()
    batch_host = atf.get_job_parameter(job_id, "BatchHost", fatal=True)
    assert (
        len(nodes) == 2 and batch_host in nodes
    ), f"Job {job_id} should run on 2 nodes including its batch host {batch_host}, got {nodes}"
    dead_node = next(node for node in nodes if node != batch_host)

    # The step's task on a node without a slurmd can't be reaped, so the
    # requeue below never finishes. A --no-kill job is only shrunk off a node
    # that is explicitly DOWN, not one that is merely not responding.
    kill_slurmd(dead_node, job_id)
    atf.run_command(
        f"scontrol update nodename={dead_node} state=down reason=test_173_1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_node_state(dead_node, "DOWN", fatal=True)
    for t in atf.timer(fatal=True):
        if atf.get_job_parameter(job_id, "NumNodes", fatal=True) == 1:
            break

    atf.run_command(
        f"scontrol requeue {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETING", fatal=True)

    # A reconfigure re-registers the stranded job with accounting.
    atf.run_command(
        "scontrol reconfigure", user=atf.properties["slurm-user"], fatal=True
    )
    wait_for_dbd_agent_drained()
