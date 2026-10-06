############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify a Prolog failure keeps a job from finishing its configuration.

With PrologFlags=Alloc a job stays CONFIGURING until every node it is waiting
on reports its Prolog done. Once one node has reported a failure, a later
success from another node must not finish the configuration and launch the
job: slurmctld records the failure on the job for that.

slurmd sends each node's result and then its own requeue back to back, so a
success from another node only lands between the two by chance, a window too
narrow to hit from outside. This test therefore sends the reports itself, as
SlurmUser, with a small libslurm client: the failure first, then the success,
and no requeue behind them. Without the recorded failure, the success finishes
the configuration and the batch script runs.

The real Prologs block on a sentinel file the whole time, so the only reports
slurmctld sees are the ones sent here, until the test releases them.

Ticket 15001.
"""

import os

import pytest

import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 15001: CONFIGURING while the Prolog runs added in 26.11",
    )
    # enable_stepmgr is only supported with PrologFlags=contain.
    atf.require_config_parameter_excludes("SlurmctldParameters", "enable_stepmgr")
    # Without DeferBatch only the batch host holds the job in CONFIGURING, so
    # it is the batch host's report that finishes the configuration. Set rather
    # than appended: an inherited NoHold or DeferBatch would change that.
    atf.require_config_parameter("PrologFlags", "Alloc")
    atf.require_nodes(2)

    # The Prolog runs as root on the node, so its sentinel needs an absolute
    # path outside the test's own cwd.
    prolog_script = str(atf.module_tmp_path / "prolog.sh")
    atf.make_bash_script(
        prolog_script,
        f"""while [ ! -f {atf.module_tmp_path}/proceed.$SLURM_JOB_ID ]; do
    sleep 0.5
done
""",
    )
    atf.require_config_parameter("Prolog", prolog_script)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def report_prolog():
    """Build the client that sends REQUEST_COMPLETE_PROLOG to slurmctld."""

    source = os.path.splitext(__file__)[0] + ".c"
    client = str(atf.module_tmp_path / "test_132_6")
    atf.compile_against_libslurm(source, client, full=True, fatal=True)

    def report(job_id, node_name, prolog_rc):
        # slurmctld only accepts this RPC from root or SlurmUser.
        output = atf.run_command_output(
            f"{client} {job_id} {node_name} {prolog_rc}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        assert output.startswith(
            "RC=0 "
        ), f"slurmctld rejected the Prolog report: {output.strip()}"

    return report


@pytest.fixture
def blocked_job():
    """Submit a two node job and release its Prologs afterwards.

    --no-requeue stops the cancel at teardown from sending the job round
    again with its Prologs still blocked.
    """

    job_id = atf.submit_job_sbatch(
        "-N2 -t2 --no-requeue -o /dev/null --wrap 'touch job_ran'", fatal=True
    )
    yield job_id
    atf.run_command(f"touch {atf.module_tmp_path / f'proceed.{job_id}'}")


def test_success_after_failure_keeps_job_configuring(report_prolog, blocked_job):
    """A success reported after a failure must not launch the job."""

    job_id = blocked_job
    assert atf.wait_for_job_state(
        job_id, "CONFIGURING"
    ), f"Job {job_id} was not held in CONFIGURING by its Prolog"

    batch_host = atf.get_job_parameter(job_id, "BatchHost", fatal=True)
    other_node = next(
        node
        for node in atf.node_range_to_list(
            atf.get_job_parameter(job_id, "NodeList", fatal=True)
        )
        if node != batch_host
    )

    # The failure comes from the other node, so only the batch host's report is
    # left for the job to wait on. That report is the success.
    report_prolog(job_id, other_node, 1)
    report_prolog(job_id, batch_host, 0)

    # A window to watch, not a wait for success: a wrongful launch moves the
    # job to RUNNING within the same RPC, so 10 seconds is plenty to see it.
    for _ in atf.timer(timeout=10, xfail=True, quiet=True):
        state = atf.get_job_parameter(job_id, "JobState", fatal=True)
        if state != "CONFIGURING":
            pytest.fail(
                f"Job {job_id} went to {state} after a Prolog success that "
                f"followed a failure"
            )
    assert not os.path.isfile(
        "job_ran"
    ), "The batch script ran although a Prolog had failed"
