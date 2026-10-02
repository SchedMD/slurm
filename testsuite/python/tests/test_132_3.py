############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""
Verify the job state while the Prolog runs for common PrologFlags settings.

With PrologFlags=Alloc the Prolog runs before the job can use its allocation,
so the job is CONFIGURING until it completes. NoHold opts out of that hold, and
DeferBatch extends it to every allocated node instead of just the batch host.
Batch, srun and salloc jobs are all covered, as they leave the CONFIGURING
state through different paths.

The Prolog blocks on a sentinel file rather than sleeping for a fixed time, so
the window being sampled stays open until the test releases it.
"""

import glob
import os

import pytest

import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup(prologs):
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 15001: CONFIGURING while the Prolog runs added in 26.11",
    )

    # Two nodes so the batch host can be released on its own. Requested before
    # require_slurm_running() so that no test case restarts the cluster.
    atf.require_nodes(2)

    # The Prolog is configured once and keys its files off the job and node it
    # runs for, so only PrologFlags changes per case. PrologFlags is read by
    # slurmctld, which scontrol reconfigure reloads synchronously, while a new
    # Prolog path has to propagate to the slurmds asynchronously.
    prolog_script = prologs.path("prolog.sh")
    atf.make_bash_script(
        prolog_script,
        f"""touch {prologs.path("started")}.$SLURM_JOB_ID.$SLURMD_NODENAME
while [ ! -f {prologs.path("proceed")}.$SLURM_JOB_ID ] &&
      [ ! -f {prologs.path("proceed")}.$SLURM_JOB_ID.$SLURMD_NODENAME ]; do
    sleep 0.5
done
touch {prologs.path("finished")}.$SLURM_JOB_ID.$SLURMD_NODENAME

# Let a case ask for a failing Prolog, either everywhere or on one node.
if [ -f {prologs.path("fail")}.$SLURM_JOB_ID ] ||
   [ -f {prologs.path("fail")}.$SLURM_JOB_ID.$SLURMD_NODENAME ]; then
    exit 1
fi
""",
    )

    atf.set_config_parameter("Prolog", prolog_script)
    atf.require_slurm_running()


@pytest.fixture
def resume_nodes():
    """Undrain any node a failing Prolog drained, so later cases still run."""

    yield
    atf.run_command(
        "scontrol update nodename=ALL state=resume",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def set_prolog_flags(prolog_flags):
    """Set PrologFlags, skipping the cases the step manager can not run.

    enable_stepmgr is only supported with PrologFlags=contain. slurmctld
    refuses to reconfigure without it and keeps the previous flags instead,
    so such a case would silently run against the wrong configuration.
    RunInJob and X11 set Contain themselves, so they are allowed too.
    """

    flags = {flag.strip() for flag in (prolog_flags or "").casefold().split(",")}
    if not flags & {"contain", "runinjob", "x11"}:
        if atf.config_parameter_includes(
            "SlurmctldParameters", "enable_stepmgr", live=False, quiet=True
        ):
            pytest.skip(
                f"enable_stepmgr is only supported with PrologFlags=contain, "
                f"which this case replaces with {prolog_flags}"
            )

    atf.set_config_parameter("PrologFlags", prolog_flags)


class Prologs:
    """The sentinel files the Prolog script and the test cases pass through.

    The Prolog runs on the slurmds with a working directory of its own, so the
    files are reached by absolute path rather than relative to the test.
    """

    def __init__(self, directory):
        self.directory = directory

    def path(self, name):
        return f"{self.directory}/{name}"

    def wait_started(self, job_id, node_count):
        """Wait until the Prolog is running (and blocked) on node_count nodes."""

        for _ in atf.timer():
            if len(glob.glob(self.path(f"started.{job_id}.*"))) >= node_count:
                break
        else:
            pytest.fail(f"Prolog did not start on {node_count} node(s) of job {job_id}")

    def wait_finished(self, job_id, node_name):
        """Wait until the Prolog has exited on node_name."""

        assert atf.wait_for_file(
            self.path(f"finished.{job_id}.{node_name}")
        ), f"Prolog did not finish on {node_name} for job {job_id}"

    def fail(self, job_id, node_name=None):
        """Make the Prolog exit non-zero, either everywhere or on one node."""

        marker = f"fail.{job_id}"
        if node_name:
            marker += f".{node_name}"
        atf.run_command(f"touch {self.path(marker)}", fatal=True)

    def release(self, job_id, node_name=None):
        """Let the Prolog finish, either everywhere or on one node."""

        marker = f"proceed.{job_id}"
        if node_name:
            marker += f".{node_name}"
        atf.run_command(f"touch {self.path(marker)}", fatal=True)


@pytest.fixture(scope="module")
def prologs():
    """Return the sentinel files of the Prolog, kept in the module's directory."""

    return Prologs(os.getcwd())


@pytest.fixture
def job_ids(prologs):
    """Collect submitted jobs and unblock their Prologs afterwards.

    A failing case then still lets its Prologs exit, before conftest cancels
    the jobs, and leaves the nodes usable for later cases.
    """

    ids = []
    yield ids
    for job_id in ids:
        prologs.release(job_id)


def get_job_nodes(job_id, node_count):
    """Return the allocated nodes of a job, asserting how many there are."""

    node_list = atf.get_job_parameter(job_id, "NodeList", default="", quiet=True)
    nodes = atf.node_range_to_list(node_list)
    assert (
        len(nodes) == node_count
    ), f"Expected {node_count} nodes for job {job_id}, got {node_list}"
    return nodes


@pytest.mark.parametrize(
    "prolog_flags,expect_configuring",
    [
        pytest.param(None, False, id="no-alloc"),
        pytest.param("Alloc", True, id="alloc"),
        pytest.param("Contain", True, id="contain"),
        pytest.param("Alloc,DeferBatch", True, id="alloc-deferbatch"),
        pytest.param("Alloc,NoHold", False, id="alloc-nohold"),
        pytest.param("Alloc,NoHold,DeferBatch", False, id="alloc-nohold-deferbatch"),
    ],
)
def test_state_while_prolog_runs(prologs, job_ids, prolog_flags, expect_configuring):
    """Verify whether a job is held in CONFIGURING while the Prolog runs."""

    output_file = "job_output"

    set_prolog_flags(prolog_flags)

    job_id = atf.submit_job_sbatch(
        f"-N1 -t5 --wrap 'touch {output_file}; sleep 30'", fatal=True
    )
    job_ids.append(job_id)

    prologs.wait_started(job_id, 1)

    state = atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND", quiet=True)
    if expect_configuring:
        assert (
            state == "CONFIGURING"
        ), f"Expected CONFIGURING while the Prolog runs, got {state}"
    else:
        # Assert the exact state rather than just "not CONFIGURING", which
        # PENDING, FAILED and NOT_FOUND would also satisfy.
        assert (
            state == "RUNNING"
        ), f"Expected RUNNING while the Prolog runs, got {state}"

    prologs.release(job_id)

    assert atf.wait_for_job_state(job_id, "RUNNING"), "Job never reached RUNNING"
    assert atf.wait_for_file(output_file), "Batch step never produced output"


@pytest.mark.parametrize(
    "prolog_flags,expect_configuring",
    [
        pytest.param("Alloc", False, id="alloc"),
        pytest.param("Alloc,DeferBatch", True, id="alloc-deferbatch"),
    ],
)
def test_state_after_batch_host_prolog(
    prologs, job_ids, prolog_flags, expect_configuring
):
    """Verify when a multi-node batch job stops configuring.

    Without DeferBatch the batch step is launched as soon as the batch host
    finishes its Prolog, so the job is running from that point. With
    DeferBatch it waits for every allocated node.
    """

    output_file = "job_output"

    set_prolog_flags(prolog_flags)

    job_id = atf.submit_job_sbatch(
        f"-N2 -t5 --wrap 'touch {output_file}; sleep 30'", fatal=True
    )
    job_ids.append(job_id)

    prologs.wait_started(job_id, 2)
    assert (
        atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND", quiet=True)
        == "CONFIGURING"
    ), "Job was not CONFIGURING while the Prolog ran"

    # Let only the batch host finish its Prolog. The other node stays
    # blocked, so the job may only leave CONFIGURING if the batch host is
    # all it was waiting on.
    batch_host = atf.get_job_parameter(job_id, "BatchHost", default=None, quiet=True)
    assert batch_host, f"Job {job_id} has no BatchHost"
    prologs.release(job_id, batch_host)

    # Wait for that Prolog to actually exit before judging the result, so
    # that a release which never took effect fails here rather than
    # reading as a successful deferral below.
    prologs.wait_finished(job_id, batch_host)

    if expect_configuring:
        # A window to watch, not a wait for success.
        for _ in atf.timer(timeout=15, xfail=True, quiet=True):
            state = atf.get_job_parameter(
                job_id, "JobState", default="NOT_FOUND", quiet=True
            )
            if state != "CONFIGURING":
                pytest.fail(
                    f"Job left CONFIGURING ({state}) before every node finished "
                    f"its Prolog"
                )
        prologs.release(job_id)

    assert atf.wait_for_job_state(job_id, "RUNNING"), "Job never reached RUNNING"
    assert atf.wait_for_file(output_file), "Batch step never produced output"


@pytest.mark.parametrize("command", ["srun", "salloc"])
@pytest.mark.parametrize(
    "prolog_flags,expect_configuring",
    [
        pytest.param("Alloc", True, id="alloc"),
        pytest.param("Alloc,NoHold", False, id="alloc-nohold"),
    ],
)
def test_state_while_prolog_runs_interactive(
    prologs, job_ids, command, prolog_flags, expect_configuring
):
    """Verify an interactive job is held in CONFIGURING while the Prolog runs."""

    started_file = "command_started"

    set_prolog_flags(prolog_flags)

    # Submitted in the background so the job id is available while srun or
    # salloc is still waiting on the Prolog.
    args = f"-N1 -t5 bash -c 'touch {started_file}; sleep 30'"
    if command == "srun":
        job_id = atf.submit_job_srun(args, background=True, fatal=True)
    else:
        job_id = atf.submit_job_salloc(args, background=True, fatal=True)
    job_ids.append(job_id)

    prologs.wait_started(job_id, 1)

    state = atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND", quiet=True)
    if expect_configuring:
        assert (
            state == "CONFIGURING"
        ), f"Expected CONFIGURING while the Prolog runs, got {state}"
        assert not os.path.isfile(
            started_file
        ), f"{command} ran the command before the Prolog finished"
    else:
        assert (
            state == "RUNNING"
        ), f"Expected RUNNING while the Prolog runs, got {state}"

    prologs.release(job_id)

    assert atf.wait_for_job_state(job_id, "RUNNING"), "Job never reached RUNNING"
    assert atf.wait_for_file(started_file), f"{command} never ran the command"


@pytest.mark.parametrize("command", ["srun", "salloc"])
def test_interactive_waits_for_every_node(prologs, job_ids, command):
    """Verify a non-batch job waits for the Prolog on all of its nodes.

    A batch job leaves CONFIGURING once its batch host finishes, but srun and
    salloc must wait for every allocated node. At -N1 those two rules are
    indistinguishable, so this allocates two nodes and releases them one at a
    time.
    """

    started_file = "command_started"

    set_prolog_flags("Alloc")

    args = f"-N2 -t5 bash -c 'touch {started_file}; sleep 30'"
    if command == "srun":
        job_id = atf.submit_job_srun(args, background=True, fatal=True)
    else:
        job_id = atf.submit_job_salloc(args, background=True, fatal=True)
    job_ids.append(job_id)

    prologs.wait_started(job_id, 2)
    assert (
        atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND", quiet=True)
        == "CONFIGURING"
    ), "Job was not CONFIGURING while the Prolog ran"

    # Release one node only. It does not matter which, as an interactive
    # job has no batch host to single out.
    first_node, second_node = get_job_nodes(job_id, 2)
    prologs.release(job_id, first_node)
    prologs.wait_finished(job_id, first_node)

    # A window to watch, not a wait for success.
    for _ in atf.timer(timeout=15, xfail=True, quiet=True):
        state = atf.get_job_parameter(
            job_id, "JobState", default="NOT_FOUND", quiet=True
        )
        if state != "CONFIGURING":
            pytest.fail(
                f"Job left CONFIGURING ({state}) after only {first_node} "
                f"finished its Prolog"
            )
    assert not os.path.isfile(
        started_file
    ), f"{command} ran the command after only {first_node} finished its Prolog"

    prologs.release(job_id, second_node)

    assert atf.wait_for_job_state(job_id, "RUNNING"), "Job never reached RUNNING"
    assert atf.wait_for_file(started_file), f"{command} never ran the command"


@pytest.mark.parametrize(
    "prolog_flags,fail_batch_host",
    [
        pytest.param("Alloc", True, id="alloc"),
        pytest.param("Alloc,DeferBatch", True, id="alloc-deferbatch-batch-host"),
        pytest.param("Alloc,DeferBatch", False, id="alloc-deferbatch-other-node"),
    ],
)
def test_prolog_failure_while_configuring(
    prologs, resume_nodes, job_ids, prolog_flags, fail_batch_host
):
    """Verify a job whose Prolog fails while CONFIGURING is not launched.

    One of the two allocated nodes fails its Prolog, which requeues and holds
    the job and drains that node. The other node finishes its Prolog only
    after the drain, so by then the job is already requeued and its success is
    ignored. This pins the requeue, hold and drain, not the case of a success
    landing before the requeue, which test_132_6 covers.

    Without DeferBatch only the batch host holds the job in CONFIGURING, so
    the batch host is the one that fails. DeferBatch holds it for every node,
    so either node failing is covered.
    """

    output_file = "job_output"

    set_prolog_flags(prolog_flags)

    job_id = atf.submit_job_sbatch(
        f"-N2 -t5 --wrap 'touch {output_file}; sleep 30'", fatal=True
    )
    job_ids.append(job_id)

    prologs.wait_started(job_id, 2)
    assert (
        atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND", quiet=True)
        == "CONFIGURING"
    ), "Job was not CONFIGURING while the Prolog ran"

    batch_host = atf.get_job_parameter(
        job_id, "BatchHost", default=None, quiet=True, fatal=True
    )
    assert batch_host, f"Job {job_id} has no BatchHost"
    node_a, node_b = get_job_nodes(job_id, 2)
    other_node = node_b if node_a == batch_host else node_a

    failed_node = batch_host if fail_batch_host else other_node
    good_node = other_node if fail_batch_host else batch_host

    prologs.fail(job_id, failed_node)
    prologs.release(job_id, failed_node)

    # slurmd sends the requeue before it re-registers the node as drained,
    # so once the node reads DRAIN the job has already been requeued.
    assert atf.wait_for_node_state(
        failed_node, "DRAIN"
    ), f"Node {failed_node} was not drained after its Prolog failed"

    # Only now let the other node succeed. Its report arrives after the
    # requeue, so this is deterministic but cannot reach the window where
    # a success lands before the requeue; test_132_6 covers that.
    prologs.release(job_id, good_node)

    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="launch_failed_requeued_held"
    ), "Job was not requeued and held after its Prolog failed"

    assert not os.path.isfile(
        output_file
    ), "Batch script ran even though the Prolog failed"


def test_prolog_failure_after_launch(prologs, resume_nodes, job_ids):
    """Verify a Prolog failing after the batch host launched the job.

    Without DeferBatch the batch script starts as soon as the batch host
    finishes its Prolog, so another node can still fail its Prolog after the
    job is running. That failure must still drain the node and requeue and
    hold the job.
    """

    output_file = "job_output"

    set_prolog_flags("Alloc")

    job_id = atf.submit_job_sbatch(
        f"-N2 -t5 --wrap 'touch {output_file}; sleep 30'", fatal=True
    )
    job_ids.append(job_id)

    prologs.wait_started(job_id, 2)
    batch_host = atf.get_job_parameter(
        job_id, "BatchHost", default=None, quiet=True, fatal=True
    )
    assert batch_host, f"Job {job_id} has no BatchHost"
    other_node = next(n for n in get_job_nodes(job_id, 2) if n != batch_host)

    prologs.release(job_id, batch_host)
    assert atf.wait_for_file(
        output_file
    ), "Batch script did not start once the batch host finished its Prolog"

    prologs.fail(job_id, other_node)
    prologs.release(job_id, other_node)

    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="launch_failed_requeued_held"
    ), f"Job was not requeued and held after {other_node} failed its Prolog"

    assert atf.wait_for_node_state(
        other_node, "DRAIN"
    ), f"Node {other_node} was not drained after its Prolog failed"
