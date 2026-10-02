############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify a job requeued by a failed Prolog runs when it is retried.

With SchedulerParameters=nohold_on_prolog_fail a Prolog failure requeues the
job without holding it, so the job is expected to try again on its own and,
once its Prolog succeeds, run. Getting back to PENDING is not enough: a
failure remembered from the first attempt must not keep the retried job
CONFIGURING forever.

The Prolog fails on a job's first attempt and succeeds on every attempt after,
so a passing run needs the second attempt to be judged on its own result.

Ticket 15001.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

# Allow for a periodic scheduling pass, not only an event-triggered one, to
# pick the requeued job back up.
RETRY_TIMEOUT = 120

# Reason of a job requeued and held by a launch failure, which this option
# exists to avoid.
HELD_REASON = "launch_failed_requeued_held"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 15001: CONFIGURING while the Prolog runs added in 26.11",
    )
    atf.require_config_parameter_includes("PrologFlags", "Alloc")
    atf.require_config_parameter_includes(
        "SchedulerParameters", "nohold_on_prolog_fail"
    )
    # A requeue otherwise waits for the job credential to expire.
    atf.require_config_parameter_includes("SchedulerParameters", ("requeue_delay", 0))

    # With PrologSlurmctld the job is CONFIGURING before the node Prolog is
    # launched, which is the path where a failure carried over from the first
    # attempt would stop the retry from ever launching the node Prolog again.
    #
    # Absolute paths below: PrologSlurmctld runs as SlurmUser on the
    # controller and Prolog as root on the node, so both need a path that
    # resolves outside the test's own cwd.
    ctld_script = str(atf.module_tmp_path / "prolog_slurmctld.sh")
    atf.make_bash_script(ctld_script, "exit 0")
    atf.set_config_parameter("PrologSlurmctld", ctld_script)

    # The attempt count is keyed by job id, which stays the same across a
    # requeue. Each attempt leaves a marker naming the node it ran on.
    prolog_script = str(atf.module_tmp_path / "prolog.sh")
    atf.make_bash_script(
        prolog_script,
        f"""attempts={atf.module_tmp_path}/attempts.$SLURM_JOB_ID
count=0
[ -f $attempts ] && count=$(cat $attempts)
count=$((count + 1))
echo $count > $attempts
touch {atf.module_tmp_path}/ran.$SLURM_JOB_ID.$count.$SLURMD_NODENAME
if [ "$count" -eq 1 ]; then
    exit 1
fi
exit 0
""",
    )
    atf.set_config_parameter("Prolog", prolog_script)

    # Two nodes, so a retry has somewhere else to go.
    atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.fixture
def resume_nodes():
    """Undrain the node a failing Prolog drained, so later tests still run."""

    yield
    # Not fatal: this runs even after a failed test, and a teardown failure
    # here would obscure the real one.
    atf.run_command(
        "scontrol update nodename=ALL state=resume",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def wait_for_attempt(job_id, attempt, timeout=atf.default_polling_timeout):
    """Wait for the Prolog to run for the given attempt; return its node."""

    # The timer polls every timeout/10 by default, which is up to 12 seconds
    # for a retry. A glob is cheap, so poll every second instead.
    for _ in atf.timer(timeout=timeout, poll_interval=1, fatal=True):
        markers = list(atf.module_tmp_path.glob(f"ran.{job_id}.{attempt}.*"))
        if markers:
            return markers[0].name.split(".", 3)[3]


def assert_requeued_unheld(job_id):
    """Assert the job was requeued once and is not held."""

    assert atf.wait_for_job_state(
        job_id, "PENDING"
    ), "Job was not requeued after its Prolog failed"

    # Read the record once so the values come from the same instant.
    job = atf.get_job(job_id)
    restarts = int(job["Restarts"])
    assert restarts == 1, f"Expected one requeue after the failure, got {restarts}"
    assert (
        job["Reason"] != HELD_REASON
    ), "nohold_on_prolog_fail should not hold the requeued job"
    priority = int(job["Priority"])
    assert priority != 0, (
        "nohold_on_prolog_fail should leave the requeued job runnable, but "
        f"its priority is {priority}"
    )


def assert_job_ran(job_id, output_file):
    """Assert the retried job ran its batch script and completed."""

    # The wrap can finish between one poll and the next, so check the file it
    # leaves behind rather than try to catch the job RUNNING.
    assert atf.wait_for_file(
        output_file
    ), "Batch script never ran after the job was requeued"
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), f"Job {job_id} did not complete after its retry"


def test_job_runs_after_prolog_failure_requeue(tmp_path, resume_nodes):
    """A job whose first Prolog fails must still run on the same node."""

    node_name = sorted(atf.get_nodes(fatal=True))[0]
    # Absolute: the wrapped command below runs on the node, not in the
    # test's own cwd.
    output_file = str(tmp_path / "job_output")

    # Pinned to one node, so the retry runs where the first attempt failed.
    # That node is drained by the failure, which also keeps the job pending
    # until the test checks it and puts the node back.
    job_id = atf.submit_job_sbatch(
        f"-w {node_name} -t2 --requeue -o /dev/null --wrap 'touch {output_file}'",
        fatal=True,
    )

    first_node = wait_for_attempt(job_id, 1)
    assert (
        first_node == node_name
    ), f"First attempt ran on {first_node}, not on {node_name}"
    assert atf.wait_for_node_state(
        node_name, "DRAIN"
    ), f"Node {node_name} was not drained after its Prolog failed"
    assert_requeued_unheld(job_id)

    atf.run_command(
        f"scontrol update nodename={node_name} state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # The retry has to launch the node Prolog again. A failure carried over
    # from the first attempt stops it here, with the job left CONFIGURING.
    second_node = wait_for_attempt(job_id, 2, timeout=RETRY_TIMEOUT)
    assert second_node == node_name, f"Retry ran on {second_node}, not on {node_name}"
    assert_job_ran(job_id, output_file)


def test_job_moves_to_another_node(tmp_path, resume_nodes):
    """A job whose first Prolog fails is dispatched to another node.

    slurm.conf documents that nohold_on_prolog_fail leaves the job unheld so
    the scheduler can dispatch it to another host. The failing node is
    drained, so the retry has to land on the other node with no
    administrator action.
    """

    # Absolute: the wrapped command below runs on the node, not in the
    # test's own cwd.
    output_file = str(tmp_path / "job_output")

    job_id = atf.submit_job_sbatch(
        f"-N1 -t2 --requeue -o /dev/null --wrap 'touch {output_file}'",
        fatal=True,
    )

    first_node = wait_for_attempt(job_id, 1)
    second_node = wait_for_attempt(job_id, 2, timeout=RETRY_TIMEOUT)
    assert (
        second_node != first_node
    ), f"Retry ran on {second_node}, which its Prolog failure should have drained"

    assert_job_ran(job_id, output_file)
    restarts = int(atf.get_job_parameter(job_id, "Restarts", fatal=True))
    assert restarts == 1, f"Expected one requeue after the failure, got {restarts}"
