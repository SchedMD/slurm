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
""",
    )

    atf.set_config_parameter("Prolog", prolog_script)
    atf.require_slurm_running()


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

    atf.set_config_parameter("PrologFlags", prolog_flags)

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

    atf.set_config_parameter("PrologFlags", prolog_flags)

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

    atf.set_config_parameter("PrologFlags", prolog_flags)

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
