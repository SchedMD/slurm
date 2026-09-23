############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that holding a job does not cut its node prolog short.

With PrologFlags=Alloc slurmctld must keep reporting the job as not ready
until every node prolog completes, whatever else happens to the job in the
meantime. `scontrol wait_job` asks exactly that question, so it stands in for
the srun step launch that the prolog is meant to gate.

The hold itself must also survive the prolog, so that a user hold is still a
user hold, and still releasable by its owner, after a later requeue.

Ticket: 25645
"""

import glob

import pytest

import atf

pytestmark = pytest.mark.slow

# How long to let `scontrol wait_job` block before calling it "still waiting".
# atf.run_command() reports a timed-out command as exit code 110.
WAIT_JOB_TIMEOUT = 20
COMMAND_TIMED_OUT = 110

# Every prolog runs on every allocated node, so the job spans more than one to
# exercise the per-node tracking rather than a single-bit special case.
NODE_COUNT = 2


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 25645: prolog tracking fixed in 26.11",
    )
    atf.require_config_parameter_includes("PrologFlags", "Alloc")

    # scontrol wait_job returns success immediately unless both power save
    # timeouts are set, so it cannot report on the prolog without them. No
    # SuspendTime is set, so nothing is actually powered down.
    atf.require_config_parameter("SuspendTimeout", 30)
    atf.require_config_parameter("ResumeTimeout", 60)

    # One prolog and one set of markers serve every test, keyed by job id. The
    # completion marker is per node, by SLURMD_NODENAME rather than hostname,
    # which is shared when the nodes are multiple slurmd on one host.
    atf.make_bash_script(
        prolog_script(),
        f"""touch {marker("started")}
for _ in $(seq 1 240); do
    [ -f {marker("proceed")} ] && break
    sleep 0.5
done
touch {marker("finished")}.$SLURMD_NODENAME
""",
    )
    atf.set_config_parameter("Prolog", prolog_script())

    atf.require_nodes(NODE_COUNT)
    atf.require_slurm_running()


def prolog_script():
    """Path of the blocking prolog, stable for the whole module."""
    return f"{atf.module_tmp_path}/prolog.sh"


def marker(name, job_id="$SLURM_JOB_ID"):
    """Path of a per-job handshake file between the test and the prolog.

    Absolute because slurmd runs the prolog, so a path relative to the test's
    own directory would resolve against the daemon's cwd instead. The default
    expands inside the prolog script itself; callers pass the job id.
    """
    return f"{atf.module_tmp_path}/{name}.{job_id}"


def get_reason(job_id):
    return atf.get_job_parameter(job_id, "Reason", fatal=True)


def assert_job_not_ready(job_id):
    """Fails if slurmctld calls the job ready while its prolog still runs."""
    # quiet=True: this call is meant to time out, and the timeout is logged as
    # a WARNING that would otherwise look like a problem in a passing run.
    result = atf.run_command(
        f"scontrol wait_job {job_id}", timeout=WAIT_JOB_TIMEOUT, quiet=True
    )
    assert (
        result["exit_code"] == COMMAND_TIMED_OUT
    ), "Job was reported ready before the node prolog finished"


def finish_prolog(job_id):
    """Unblocks the prolog and waits for it to finish on every node.

    The wait is on the prolog's own markers rather than on slurmctld, because
    a controller that wrongly believes the prolog is done would let this
    return early and leave the caller testing the wrong thing.
    """
    atf.run_command(f"touch {marker('proceed', job_id)}", fatal=True)

    pattern = f"{marker('finished', job_id)}.*"
    for t in atf.timer(fatal=True):
        if len(glob.glob(pattern)) == NODE_COUNT:
            break

    result = atf.run_command(f"scontrol wait_job {job_id}")
    assert result["exit_code"] == 0, "Job never became ready after the prolog"


@pytest.fixture(scope="function")
def prolog_job():
    """Submits a job whose node prolog blocks until it is unblocked.

    Yields the job id with the job sitting at Reason=Prolog. Teardown always
    unblocks the prolog so a failing test cannot leave it spinning.
    """
    job_id = atf.submit_job_sbatch(
        f"-J test_132_2 -N{NODE_COUNT} -t 5 --requeue -o /dev/null "
        "--wrap 'sleep infinity'",
        fatal=True,
    )

    assert atf.wait_for_file(marker("started", job_id)), "Prolog did not start"
    assert get_reason(job_id) == "Prolog", "Job should be waiting on its prolog"

    yield job_id

    # Not fatal: this runs even when the test already failed, and a teardown
    # failure here would obscure the real one.
    atf.run_command(f"touch {marker('proceed', job_id)}")


def test_prolog_gates_job_ready(prolog_job):
    """A job whose node prolog is running is not ready. Baseline for the rest."""

    assert_job_not_ready(prolog_job)
    finish_prolog(prolog_job)


@pytest.mark.parametrize(
    "hold_command, held_reason",
    [("hold", "JobHeldAdmin"), ("uhold", "JobHeldUser")],
)
def test_hold_during_prolog_keeps_job_unready(prolog_job, hold_command, held_reason):
    """Holding a job must not make slurmctld call it ready early."""

    atf.run_command(f"scontrol {hold_command} {prolog_job}", user="root", fatal=True)

    # scontrol update is synchronous, so the reason is deterministic here. The
    # hold is reported at once; the prolog is tracked separately.
    assert (
        get_reason(prolog_job) == held_reason
    ), f"Hold placed during the prolog should report {held_reason}"

    assert_job_not_ready(prolog_job)
    finish_prolog(prolog_job)


def test_release_during_prolog_keeps_job_unready(prolog_job):
    """Releasing a hold mid-prolog must not make the job ready early."""

    atf.run_command(f"scontrol uhold {prolog_job}", user="root", fatal=True)
    atf.run_command(f"scontrol release {prolog_job}", fatal=True)

    assert_job_not_ready(prolog_job)
    finish_prolog(prolog_job)


@pytest.mark.parametrize(
    "hold_command, held_reason",
    [("hold", "JobHeldAdmin"), ("uhold", "JobHeldUser")],
)
def test_hold_survives_prolog_and_requeue(prolog_job, hold_command, held_reason):
    """A hold placed during the prolog keeps its type across a requeue."""

    atf.run_command(f"scontrol {hold_command} {prolog_job}", user="root", fatal=True)
    finish_prolog(prolog_job)

    assert (
        get_reason(prolog_job) == held_reason
    ), f"{hold_command} was not preserved across prolog completion"

    atf.run_command(f"scontrol requeue {prolog_job}", fatal=True)

    # Poll on the reason too: a freshly requeued job transits Cleaning before
    # the scheduler re-derives the hold reason, so a bare PENDING wait would
    # race and could read either Cleaning or the pre-requeue value.
    atf.wait_for_job_state(
        prolog_job, "PENDING", desired_reason=held_reason, fatal=True
    )


def test_hold_during_prolog_survives_restart(prolog_job):
    """A hold placed mid-prolog survives a slurmctld restart with the prolog."""

    atf.run_command(f"scontrol uhold {prolog_job}", user="root", fatal=True)
    atf.restart_slurmctld()

    assert (
        get_reason(prolog_job) == "JobHeldUser"
    ), "User hold was lost across a slurmctld restart"
    assert_job_not_ready(prolog_job)

    finish_prolog(prolog_job)


def test_owner_cannot_release_admin_hold(prolog_job):
    """An admin hold placed during the prolog is not releasable by the owner."""

    atf.run_command(f"scontrol hold {prolog_job}", user="root", fatal=True)
    finish_prolog(prolog_job)

    # Only this case needs an unprivileged test user, since only it asserts
    # that a release is DENIED. Note that slurmctld also permits a release by
    # AdminLevel>=Operator, which is_super_user() does not cover.
    if atf.is_super_user():
        pytest.skip(
            "Ticket 25645: a denied release needs an unprivileged SlurmTestUser"
        )

    result = atf.run_command(f"scontrol release {prolog_job}", xfail=True)
    assert result["exit_code"] != 0, "Owner was able to release an admin hold"
    assert (
        "Access/permission denied" in result["stderr"]
    ), f"Expected a permission denial, got: {result['stderr']}"
