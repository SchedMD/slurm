############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Tests for srun --async (asynchronous job step submission).

`srun --async` submits a step and exits 0 once the controller accepts the
create request, without waiting for the step to launch or finish. The step
is supervised by the job's step manager (stepmgr) running inside the batch
slurmstepd, so all tests must keep the batch step alive long enough for the
async step to complete.

This file also covers the related --parsable output: the submit message
moved from stderr info() to stdout printf so the step id can be captured
with $(...). --parsable strips the prefix to mirror sbatch --parsable, and
on srun it requires --async.
"""

import re

import pytest

import atf

PARSABLE_RE = re.compile(r"^(\d+)\.(\d+)$", re.MULTILINE)

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "bin/srun",
        reason="Issue 50739: srun --async was added in 26.05",
    )
    # stepmgr is enabled when both SlurmctldParameters=enable_stepmgr
    # and PrologFlags=Contain are set.
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_accounting()
    # test_async_rejected_in_hetjob submits a 2-component heterogeneous job.
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_slurm_running()


def _run_in_alloc(srun_cmd):
    """Run an srun command inside a sbatch allocation and return stdout/stderr."""
    out_file = "srun_out.txt"
    err_file = "srun_err.txt"

    job_id = atf.submit_job_sbatch(
        f"-N1 -o /dev/null --wrap '"
        f"{srun_cmd} >{out_file} 2>{err_file}; "
        f"echo srun_rc=$? >>{out_file}; sync'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    return (
        atf.run_command_output(f"cat {out_file}", fatal=True),
        atf.run_command_output(f"cat {err_file}", fatal=True),
    )


def test_async_stdout():
    """srun --async writes "Submitted step <jobid>.<stepid>" to stdout."""
    stdout, stderr = _run_in_alloc("srun --async true")

    assert re.search(
        r"^Submitted step \d+\.\d+$", stdout, re.MULTILINE
    ), f"expected 'Submitted step <jobid>.<stepid>' in stdout, got: {stdout!r}"
    assert (
        "Submitted step" not in stderr
    ), f"submit message should not appear on stderr, got: {stderr!r}"
    assert "srun_rc=0" in stdout, f"srun should exit 0, got stdout: {stdout!r}"


def test_srun_parsable_async():
    """srun --async --parsable writes bare "<jobid>.<stepid>" to stdout."""
    stdout, stderr = _run_in_alloc("srun --async --parsable true")

    match = re.search(r"^(\d+\.\d+)$", stdout, re.MULTILINE)
    assert match, f"expected bare '<jobid>.<stepid>' in stdout, got: {stdout!r}"
    assert (
        "Submitted step" not in stdout
    ), f"--parsable should strip the prefix, got: {stdout!r}"
    assert (
        "Submitted step" not in stderr
    ), f"submit message should not appear on stderr, got: {stderr!r}"
    assert "srun_rc=0" in stdout, f"srun should exit 0, got stdout: {stdout!r}"


def test_srun_parsable_without_async():
    """srun --parsable without --async errors at option-validation."""
    result = atf.run_command("srun --parsable -N1 true", xfail=True)

    assert result["exit_code"] != 0, "--parsable without --async should fail"
    assert (
        "--parsable requires --async" in result["stderr"]
    ), f"expected validation error, got stderr: {result['stderr']!r}"


def test_async_submit_and_complete():
    """srun --async submits a step, exits 0 with stepid, step runs to completion."""
    out_file = "async.out"

    script = "submit_and_complete.sh"
    atf.make_bash_script(
        script,
        f"srun --async -o {out_file} bash -c 'echo hello-from-async'\nswait\n",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t1 {script}", fatal=True)

    assert atf.wait_for_step_accounted(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(out_file, fatal=True)
    assert "hello-from-async" in atf.run_command_output(
        f"cat {out_file}", fatal=True
    ), f"Async step output file {out_file} should contain step stdout"


@pytest.mark.parametrize(
    "step_cmd, expected_rc",
    [("true", 0), ("false", 1)],
)
def test_async_exit_code_in_sacct(step_cmd, expected_rc):
    """srun --async always returns 0; the step's real rc surfaces in sacct."""
    out_file = f"exit_{step_cmd}.out"

    job_id = atf.submit_job_sbatch(
        f"-N1 -n1 -t1 --wrap 'srun --async -o {out_file} {step_cmd}; swait'",
        fatal=True,
    )
    assert atf.wait_for_step_accounted(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    # sacct ExitCode is "<rc>:<signal>"; check the rc half.
    output = atf.run_command_output(
        f"sacct -j {job_id}.0 --noheader -P -o ExitCode",
        fatal=True,
    ).strip()
    assert output, f"sacct returned no ExitCode for step {job_id}.0"
    rc = int(output.splitlines()[0].split(":")[0])
    assert (
        rc == expected_rc
    ), f"Expected step ExitCode rc={expected_rc} for `{step_cmd}`, got {output!r}"


@pytest.mark.parametrize(
    "conflict_arg, error_re",
    [
        ("--pty", r"--async and --pty are mutually exclusive"),
        ("--immediate", r"--immediate and --async are mutually exclusive"),
    ],
)
def test_async_mutex_with_other_options(conflict_arg, error_re):
    """srun rejects --async + --pty / --immediate before submission."""
    result = atf.run_command(f"srun --async {conflict_arg} hostname", xfail=True)
    assert (
        result["exit_code"] != 0
    ), f"`srun --async {conflict_arg}` should fail validation"
    assert re.search(
        error_re, result["stderr"]
    ), f"Expected stderr matching {error_re!r}; got {result['stderr']!r}"


def test_async_rejected_outside_existing_allocation():
    """`srun --async` is rejected when srun is being used for job submission.

    --async needs a stepmgr running in the job's allocation; for an
    interactive submission (srun creating its own alloc) there is no
    batch slurmstepd to host stepmgr, and srun exits before any
    follow-up step could start one. The option verifier rejects --async
    at parse time when neither SLURM_JOB_ID nor --jobid is set.
    """
    result = atf.run_command("srun --async -t1 hostname", xfail=True)
    assert (
        result["exit_code"] != 0
    ), f"srun --async outside an existing allocation should fail: {result!r}"
    assert (
        "--async is only valid for steps submitted within an existing job allocation"
        in result["stderr"]
    ), (
        "Expected stderr with the outside-allocation rejection message; "
        f"got stderr={result['stderr']!r}"
    )


def test_async_queued_when_busy():
    """A second `--async --exclusive` step queues until the first completes."""
    out1 = "queued1.out"
    out2 = "queued2.out"

    marker = "queued_marker"
    script = "queued_submit.sh"
    atf.make_bash_script(
        script,
        f"""srun --async --parsable --exclusive -o {out1} sleep 10 > {marker} 2>&1
srun --async --parsable --exclusive -o {out2} sleep 1 >> {marker} 2>&1
swait
""",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t2 {script}", fatal=True)

    atf.wait_for_file(marker, fatal=True)

    text = ""
    for _t in atf.timer():
        text = atf.run_command_output(f"cat {marker}", quiet=True)
        if len(PARSABLE_RE.findall(text)) >= 2:
            break
    else:
        pytest.fail(f"Expected two 'Submitted step' lines, got: {text!r}")

    matches = PARSABLE_RE.findall(text)
    step_ids = [int(s) for _j, s in matches[:2]]

    second_key = f"{job_id}.{step_ids[1]}"
    saw_pending = False
    # Poll fast: the queued step may be PENDING only briefly.
    for _t in atf.timer(poll_interval=0.5, quiet=True):
        steps = atf.get_steps(job_id, quiet=True)
        if second_key in steps and steps[second_key].get("State") == "PENDING":
            saw_pending = True
            break
    assert saw_pending, (
        f"Second --async --exclusive step {second_key} should queue in "
        f"PENDING while the first step holds the resource"
    )

    for s in step_ids:
        assert atf.wait_for_step_accounted(
            job_id, s, fatal=True
        ), f"async step {job_id}.{s} should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)


def test_async_scancel():
    """`scancel <jobid>.<stepid>` ends a running async step in CANCELLED state."""
    out_file = "scancel.out"

    job_id = atf.submit_job_sbatch(
        f"-N1 -n1 -t1 --wrap 'srun --async -o {out_file} sleep 60; sleep 30'",
        fatal=True,
    )

    assert atf.wait_for_step(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should appear in the step list"

    atf.run_command(f"scancel {job_id}.0", fatal=True)

    # sacct State may appear as "CANCELLED" or "CANCELLED by <uid>".
    state_text = ""
    for _t in atf.timer():
        state_text = atf.run_command_output(
            f"sacct -j {job_id}.0 --noheader -P -o State",
            quiet=True,
        ).strip()
        if state_text and state_text.splitlines()[0].startswith("CANCELLED"):
            break
    else:
        pytest.fail(
            f"Async step {job_id}.0 did not transition to CANCELLED: " f"{state_text!r}"
        )


def test_async_rejected_in_hetjob():
    """srun --async fails inside a heterogeneous job.

    Hetjobs cannot use stepmgr, so the create request is rejected.
    """
    marker = "het_marker"
    out_file = "het.out"

    script = "het.sh"
    atf.make_bash_script(
        script,
        f"""#SBATCH --ntasks=1 --cpus-per-task=1 -t1
#SBATCH hetjob
#SBATCH --ntasks=1 --cpus-per-task=1 -t1

srun --async --parsable -o {out_file} hostname > {marker} 2>&1
echo "srun_rc=$?" >> {marker}
""",
    )
    job_id = atf.submit_job_sbatch(f"-t1 {script}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(marker, fatal=True)

    text = atf.run_command_output(f"cat {marker}", fatal=True)
    assert re.search(
        r"srun_rc=[1-9]\d*", text
    ), f"srun --async should fail in a hetjob but succeeded: {text!r}"
    assert not PARSABLE_RE.search(
        text
    ), f"srun --async in a hetjob should not return a step id: {text!r}"


def test_async_split_output_error():
    """--output and --error route stdout/stderr to separate compute-node files."""
    out_file = "split.out"
    err_file = "split.err"

    script = "split_output.sh"
    atf.make_bash_script(
        script,
        f"srun --async -o {out_file} -e {err_file} bash -c 'echo to-stdout; echo to-stderr >&2'\nswait\n",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t1 {script}", fatal=True)
    assert atf.wait_for_step_accounted(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.wait_for_file(out_file, fatal=True)
    atf.wait_for_file(err_file, fatal=True)
    out_text = atf.run_command_output(f"cat {out_file}", fatal=True)
    err_text = atf.run_command_output(f"cat {err_file}", fatal=True)

    assert (
        "to-stdout" in out_text and "to-stderr" not in out_text
    ), f"--output should contain only stdout: {out_text!r}"
    assert (
        "to-stderr" in err_text and "to-stdout" not in err_text
    ), f"--error should contain only stderr: {err_text!r}"


def test_async_default_output_filename():
    """Without --output, an async step writes to `slurm-<jobid>.<stepid>.out`.

    A NULL output_filename for an async step expands to "slurm-%J.out"
    (where %J is jobid.stepid), relative to the task working directory.
    """
    script = "default_submit.sh"
    atf.make_bash_script(
        script,
        "srun --async hostname\nswait\n",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t1 {script}", fatal=True)
    step_id = 0
    assert atf.wait_for_step_accounted(
        job_id, step_id, fatal=True
    ), f"async step {job_id}.{step_id} should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    expected_file = f"slurm-{job_id}.{step_id}.out"
    atf.wait_for_file(expected_file, fatal=True)
    assert atf.run_command_output(
        f"cat {expected_file}", fatal=True
    ).strip(), f"Default output file {expected_file} should contain step stdout"


def test_async_concurrent_steps_fan_out():
    """Several --async steps fired in succession all run and complete.

    Uses --exact -n1 so each step occupies a single CPU and they do not
    take whole-node exclusivity; the -N2 -n4 allocation provides 4 CPU
    slots so all four async steps can run concurrently.
    """
    num_steps = 4

    out_files = [f"concurrent_{i}.out" for i in range(num_steps)]

    marker = "concurrent_marker"

    script = "concurrent_submit.sh"
    srun_lines = "\n".join(
        f"srun --async --parsable --exact -n1 -o {out_files[i]} "
        f"bash -c 'sleep 1; echo done-{i}' >> {marker} 2>&1"
        for i in range(num_steps)
    )
    atf.make_bash_script(
        script,
        f"""{srun_lines}
swait
""",
    )
    job_id = atf.submit_job_sbatch(
        f"-N2 -n{num_steps} -t1 {script}",
        fatal=True,
    )
    atf.wait_for_file(marker, fatal=True)

    text = ""
    for _t in atf.timer():
        text = atf.run_command_output(f"cat {marker}", quiet=True)
        if len(PARSABLE_RE.findall(text)) >= num_steps:
            break
    else:
        pytest.fail(f"Expected {num_steps} 'Submitted step' lines, got: {text!r}")

    matches = PARSABLE_RE.findall(text)
    step_ids = [int(s) for _j, s in matches[:num_steps]]
    assert (
        len(set(step_ids)) == num_steps
    ), f"Each async step should get a distinct step id, got {step_ids!r}"

    for sid in step_ids:
        assert atf.wait_for_step_accounted(
            job_id, sid, fatal=True
        ), f"async step {job_id}.{sid} should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    for i, out in enumerate(out_files):
        atf.wait_for_file(out, fatal=True)
        text = atf.run_command_output(f"cat {out}", fatal=True)
        assert (
            f"done-{i}" in text
        ), f"Output for step {i} should contain its marker; got {text!r}"


def test_async_sattach_does_not_succeed():
    """sattach against a running --async step must not return success.

    Async steps don't open the IO listener that sattach relies on, so the
    expected outcomes are an explicit error or a timeout. A successful
    (exit 0) sattach would indicate a regression where a non-IO step is
    accidentally exposing an IO connection.
    """
    out_file = "sattach.out"

    job_id = atf.submit_job_sbatch(
        f"-N1 -n1 -t1 --wrap 'srun --async -o {out_file} sleep 30; sleep 30'",
        fatal=True,
    )
    assert atf.wait_for_step(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should appear in the step list"

    # Short timeout: sattach is expected to hang on an async step.
    result = atf.run_command(f"sattach {job_id}.0", timeout=10, quiet=True)
    assert (
        result["exit_code"] != 0
    ), f"sattach should not succeed against an --async step: {result!r}"

    atf.run_command(f"scancel {job_id}.0", quiet=True)


def test_async_output_filename_per_task_substitution():
    """`-o foo-%t.out` produces a separate output file per task for an async step.

    Filename pattern substitution is performed on the compute node when
    the task starts, independent of whether srun is sync or async. With
    `-n3 -O` and `-o tasks-%t.out`, three distinct files (one per task)
    must be written, each containing only that task's output.
    """
    num_tasks = 3
    out_pattern = "tasks-%t.out"

    expected_files = [f"tasks-{i}.out" for i in range(num_tasks)]

    script = "per_task_subst.sh"
    atf.make_bash_script(
        script,
        f"srun --async -n{num_tasks} -O -o {out_pattern} bash -c 'echo task-$SLURM_PROCID'\nswait\n",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n{num_tasks} -O -t1 {script}", fatal=True)
    assert atf.wait_for_step_accounted(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should be recorded in accounting"

    for i, f in enumerate(expected_files):
        atf.wait_for_file(f, fatal=True)
        text = atf.run_command_output(f"cat {f}", fatal=True).strip()
        assert (
            text == f"task-{i}"
        ), f"Per-task output for task {i} should be 'task-{i}'; got {text!r}"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)


def test_async_step_killed_when_alloc_ends():
    """An async step is terminated when its enclosing allocation tears down.

    Stepmgr lives in the batch slurmstepd, so when the batch script ends,
    the step manager ends with it; any in-flight async steps must be
    cancelled rather than orphaned. Submits a `sleep 120` async step then
    immediately exits the batch script (no keepalive). The step cannot
    have COMPLETED naturally; sacct must show a non-COMPLETED terminal
    state.
    """
    out_file = "teardown.out"

    job_id = atf.submit_job_sbatch(
        f"-N1 -n1 -t1 --wrap 'srun --async -o {out_file} sleep 120'",
        fatal=True,
    )

    atf.wait_for_step_accounted(job_id, 0, fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    state = (
        atf.run_command_output(
            f"sacct -j {job_id}.0 --noheader -P -o State",
            fatal=True,
        )
        .strip()
        .splitlines()[0]
    )

    assert state and not state.startswith("COMPLETED"), (
        f"Async step should reach a non-COMPLETED terminal state when its "
        f"allocation ends; got State={state!r}"
    )


def test_async_step_time_limit():
    """Step-level --time on srun --async is enforced after srun has exited.

    A 1-minute step --time on a 5-minute sleep must end the step well
    before the sleep would naturally complete, with sacct State=TIMEOUT.
    """
    out_file = "step_time.out"

    job_id = atf.submit_job_sbatch(
        f"-N1 -n1 -t5 --wrap 'srun --async --time=1 -o {out_file} sleep 300; swait'",
        fatal=True,
    )
    atf.wait_for_step_accounted(job_id, 0, timeout=90, fatal=True)
    state = ""
    # Step --time=1 trips TIMEOUT near 60s, beyond the 45s default.
    for _t in atf.timer(90, quiet=True):
        state = (
            atf.run_command_output(
                f"sacct -j {job_id}.0 --noheader -P -o State",
                quiet=True,
            )
            .strip()
            .splitlines()[0]
        )
        if state and not state.startswith(("RUNNING", "PENDING")):
            break

    atf.wait_for_job_state(job_id, "DONE", timeout=120, fatal=True)

    assert state.startswith("TIMEOUT"), (
        f"Async step with --time=1 should end as TIMEOUT well before its "
        f"5-minute sleep finishes; got State={state!r}"
    )


def test_async_multi_node_single_step():
    """A single async step distributed across multiple nodes runs tasks on each.

    Exercises the step-layout / task-distribution path for an async step
    that spans nodes, separately from the concurrent-fan-out test where
    each step is single-task. Uses `-o foo-%n.out` so each node's task
    writes to its own file, and emits `$SLURMD_NODENAME` (rather than
    `hostname(1)`) so test environments where multiple Slurm nodes share
    one OS hostname still see distinct values.
    """
    out_pattern = "multinode-%n.out"
    expected_files = [f"multinode-{i}.out" for i in range(2)]

    script = "multinode.sh"
    atf.make_bash_script(
        script,
        f"srun --async -N2 -n2 -o {out_pattern} bash -c 'echo $SLURMD_NODENAME'\nswait\n",
    )
    job_id = atf.submit_job_sbatch(f"-N2 -n2 -t1 {script}", fatal=True)
    atf.wait_for_step_accounted(job_id, 0, fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    node_names = []
    for f in expected_files:
        atf.wait_for_file(f, fatal=True)
        text = atf.run_command_output(f"cat {f}", fatal=True).strip()
        assert text, f"Per-node output {f} should contain a node name; got empty"
        node_names.append(text)

    assert len(set(node_names)) == 2, (
        f"A 2-node async step should run tasks on two distinct Slurm nodes; "
        f"got {node_names!r}"
    )


def test_async_scancel_pending():
    """`scancel <jobid>.<stepid>` cancels a PENDING (queued) async step.

    Exercises the async-only signal path: the pending step is removed
    and a jobacct completion record is written so sacct shows CANCELLED.
    """
    marker = "pending_cancel_marker"
    script = "pending_cancel_submit.sh"
    atf.make_bash_script(
        script,
        f"""srun --async --parsable --exclusive -o /dev/null sleep 10 > {marker} 2>&1
srun --async --parsable --exclusive -o /dev/null sleep 5 >> {marker} 2>&1
swait
""",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t3 {script}", fatal=True)
    atf.wait_for_file(marker, fatal=True)

    text = ""
    for _t in atf.timer():
        text = atf.run_command_output(f"cat {marker}", quiet=True)
        if len(PARSABLE_RE.findall(text)) >= 2:
            break
    else:
        pytest.fail(f"Expected two 'Submitted step' lines, got: {text!r}")
    matches = PARSABLE_RE.findall(text)
    queued_step = int(matches[1][1])
    queued_key = f"{job_id}.{queued_step}"

    saw_pending = False
    # Poll fast: the queued step may be PENDING only briefly.
    for _t in atf.timer(poll_interval=0.5, quiet=True):
        steps = atf.get_steps(job_id, quiet=True)
        if steps.get(queued_key, {}).get("State") == "PENDING":
            saw_pending = True
            break
    assert saw_pending, (
        f"{queued_key} never reached PENDING; cannot exercise the "
        f"pending-async signal path"
    )

    res = atf.run_command(f"scancel {job_id}.{queued_step}", fatal=True)
    assert (
        res["exit_code"] == 0
    ), f"scancel of pending step {job_id}.{queued_step} should succeed: {res!r}"

    gone = False
    for _t in atf.timer(quiet=True):
        steps = atf.get_steps(job_id, quiet=True)
        if queued_key not in steps:
            gone = True
            break
    assert gone, f"{queued_key} should be gone from scontrol after scancel"

    assert atf.wait_for_step_accounted(
        job_id, queued_step, fatal=True
    ), f"queued async step {job_id}.{queued_step} should be recorded in accounting"
    state = (
        atf.run_command_output(
            f"sacct -j {job_id}.{queued_step} --noheader -P -o State",
            quiet=True,
        )
        .strip()
        .splitlines()[0]
    )
    assert state.startswith(
        "CANCELLED"
    ), f"Queued async step should be CANCELLED in sacct; got {state!r}"

    atf.wait_for_job_state(job_id, "DONE", timeout=120, fatal=True)


@pytest.fixture(scope="function")
def _mpi_ports_minimal():
    """Configure MpiParams with the minimum port range the job needs.

    A 3-port range lets one step reserve 2 ports so the next async step
    hits ESLURM_PORTS_BUSY on the remaining single port.
    """
    atf.add_config_parameter_value("MpiParams", "ports=60000-60002")
    atf.restart_slurm()
    yield
    atf.remove_config_parameter_value("MpiParams", "ports=60000-60002")
    atf.restart_slurm()


def test_async_queued_on_resv_port_exhaustion(_mpi_ports_minimal):
    """`srun --async` queues with ESLURM_STEP_QUEUED when resv ports busy.

    When `resv_port_step_alloc` returns ESLURM_PORTS_BUSY, an async step
    is added to the pending queue and srun reports success; a non-async
    step would return the error instead.
    """
    marker = "ports_marker"
    ready = "ports_ready"
    script = "ports_submit.sh"
    # --overlap lets both steps share the single CPU so the second step
    # reaches the port-busy gate rather than queueing on CPU contention.
    # The ready marker ensures step 1's ports are reserved before the
    # async step runs its port-alloc.
    atf.make_bash_script(
        script,
        f"""srun --overlap -N1 -n1 bash -c 'echo ready > {ready}; sleep 10' &
while [ ! -f {ready} ]; do sleep 0.2; done
srun --async --parsable --overlap -N1 -n1 sleep 1 > {marker} 2>&1
wait
""",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t2 {script}", fatal=True)
    atf.wait_for_file(marker, fatal=True)

    text = ""
    for _t in atf.timer():
        text = atf.run_command_output(f"cat {marker}", quiet=True)
        if PARSABLE_RE.search(text):
            break
    m = PARSABLE_RE.search(text)
    assert m, f"--async submission should succeed (queued); got {text!r}"
    queued_step = int(m.group(2))
    queued_key = f"{job_id}.{queued_step}"

    saw_pending = False
    # Poll fast: the queued step may be PENDING only briefly.
    for _t in atf.timer(poll_interval=0.5, quiet=True):
        steps = atf.get_steps(job_id, quiet=True)
        if steps.get(queued_key, {}).get("State") == "PENDING":
            saw_pending = True
            break
    assert saw_pending, (
        f"--async on port-busy should queue PENDING, not error; "
        f"got steps={atf.get_steps(job_id, quiet=True)!r}"
    )

    assert atf.wait_for_step_accounted(
        job_id, queued_step, fatal=True
    ), f"queued async step {job_id}.{queued_step} should be recorded in accounting"
    atf.run_command(f"scancel {job_id}", quiet=True)
    atf.wait_for_job_state(job_id, "DONE", timeout=120, fatal=True)


# ---------------------------------------------------------------------------
# Output filename substitution patterns
# ---------------------------------------------------------------------------


def test_async_output_filename_pattern_j():
    """`-o foo-%j.out` embeds the numeric job id in the output filename.

    %j is a common pattern in user scripts to distinguish per-job output
    files; it must expand to the same job id that sbatch reported.
    """
    out_pattern = "pat_j-%j.out"

    script = "pat_j_submit.sh"
    atf.make_bash_script(
        script,
        f"srun --async -o {out_pattern} bash -c 'echo jobid-$SLURM_JOB_ID'\nswait\n",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t1 {script}", fatal=True)
    assert atf.wait_for_step_accounted(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    expected = f"pat_j-{job_id}.out"
    atf.wait_for_file(expected, fatal=True)
    assert f"jobid-{job_id}" in atf.run_command_output(
        f"cat {expected}", fatal=True
    ), f"%j output file {expected} should contain the job id"


def test_async_output_filename_pattern_J():
    """`-o foo-%J.out` embeds the job.step id in the output filename.

    %J expands to `<jobid>.<stepid>`, giving a file that is unique per step.
    """
    out_pattern = "pat_J-%J.out"

    script = "pat_J_submit.sh"
    atf.make_bash_script(
        script,
        f"srun --async -o {out_pattern} bash -c 'echo hello'\nswait\n",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t1 {script}", fatal=True)
    assert atf.wait_for_step_accounted(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    expected = f"pat_J-{job_id}.0.out"
    atf.wait_for_file(expected, fatal=True)
    assert "hello" in atf.run_command_output(
        f"cat {expected}", fatal=True
    ), f"%J output file {expected} should contain step output"


def test_async_output_filename_pattern_s():
    """`-o foo-%s.out` embeds the step id in the output filename.

    Two sequential async steps produce distinct %s-expanded filenames.
    """
    out_pattern = "pat_s-%s.out"

    script = "pat_s_submit.sh"
    marker = "pat_s_marker"
    atf.make_bash_script(
        script,
        f"""srun --async --parsable -o {out_pattern} bash -c 'echo step-$SLURM_STEPID' >> {marker} 2>&1
srun --async --parsable -o {out_pattern} bash -c 'echo step-$SLURM_STEPID' >> {marker} 2>&1
swait
""",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t1 {script}", fatal=True)

    text = ""
    for _t in atf.timer():
        text = atf.run_command_output(f"cat {marker}", quiet=True)
        if len(PARSABLE_RE.findall(text)) >= 2:
            break
    else:
        pytest.fail(f"Expected two parsable step-id lines, got: {text!r}")

    matches = PARSABLE_RE.findall(text)
    step_ids = [int(sid) for _jid, sid in matches[:2]]
    for sid in step_ids:
        assert atf.wait_for_step_accounted(
            job_id, sid, fatal=True
        ), f"async step {job_id}.{sid} should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    for sid in step_ids:
        expected = f"pat_s-{sid}.out"
        atf.wait_for_file(expected, fatal=True)
        assert f"step-{sid}" in atf.run_command_output(
            f"cat {expected}", fatal=True
        ), f"%s output file {expected} should contain 'step-{sid}'"


# ---------------------------------------------------------------------------
# Resource request options
# ---------------------------------------------------------------------------


def test_async_ntasks_request():
    """An async step with -n N launches exactly N tasks.

    Sync srun with -n N reliably launches N tasks; async must too.
    Each task writes its SLURM_PROCID to a shared output file so we can
    count the distinct task ids.
    """
    num_tasks = 4
    out_file = "ntasks.out"

    script = "ntasks_submit.sh"
    atf.make_bash_script(
        script,
        f"srun --async -n{num_tasks} -O -o {out_file} bash -c 'echo $SLURM_PROCID'\nswait\n",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n{num_tasks} -O -t1 {script}", fatal=True)
    assert atf.wait_for_step_accounted(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should be recorded in accounting"

    atf.wait_for_file(out_file, fatal=True)
    procids = set(atf.run_command_output(f"cat {out_file}", fatal=True).split())
    assert procids == {
        str(i) for i in range(num_tasks)
    }, f"Expected task procids {{0..{num_tasks-1}}}, got {procids!r}"

    ntasks_str = (
        atf.run_command_output(
            f"sacct -j {job_id}.0 --noheader -P -o NTasks", fatal=True
        )
        .strip()
        .splitlines()[0]
    )
    assert ntasks_str == str(
        num_tasks
    ), f"sacct NTasks should be {num_tasks}; got {ntasks_str!r}"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)


def test_async_cpus_per_task():
    """An async step with -c N allocates N CPUs per task.

    SLURM_CPUS_PER_TASK is set by the slurmstepd from the step allocation;
    an async step must see the same value as a sync step.
    """
    cpus = 2
    out_file = "cpt.out"

    script = "cpt_submit.sh"
    atf.make_bash_script(
        script,
        f"srun --async -n1 -c{cpus} -o {out_file} bash -c 'echo $SLURM_CPUS_PER_TASK'\nswait\n",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -c{cpus} -t1 {script}", fatal=True)
    assert atf.wait_for_step_accounted(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.wait_for_file(out_file, fatal=True)
    value = atf.run_command_output(f"cat {out_file}", fatal=True).strip()
    assert value == str(
        cpus
    ), f"SLURM_CPUS_PER_TASK should be {cpus} inside async step; got {value!r}"


# ---------------------------------------------------------------------------
# SLURM environment variables
# ---------------------------------------------------------------------------


def test_async_slurm_env_vars():
    """An async step receives the standard SLURM_* environment variables.

    Verifies the variables most likely to be read by user scripts:
    SLURM_JOB_ID, SLURM_STEPID, SLURM_NNODES, SLURM_NTASKS,
    SLURM_PROCID.  All are set by slurmstepd at task launch; the async
    path must not bypass that setup.
    """
    num_tasks = 2
    out_pattern = "envvars-%t.out"
    expected_files = [f"envvars-{i}.out" for i in range(num_tasks)]

    script = "envvars_submit.sh"
    atf.make_bash_script(
        script,
        f"srun --async -N2 -n{num_tasks} -o {out_pattern} "
        f"bash -c 'echo $SLURM_JOB_ID $SLURM_STEPID $SLURM_NNODES "
        f"$SLURM_NTASKS $SLURM_PROCID'\nswait\n",
    )
    job_id = atf.submit_job_sbatch(f"-N2 -n2 -t1 {script}", fatal=True)
    assert atf.wait_for_step_accounted(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    lines = []
    for f in expected_files:
        atf.wait_for_file(f, fatal=True)
        lines.append(atf.run_command_output(f"cat {f}", fatal=True).strip())

    assert (
        len(lines) == num_tasks
    ), f"Expected {num_tasks} per-task output files; got {lines!r}"

    procids = set()
    for line in lines:
        parts = line.split()
        assert len(parts) == 5, f"Expected 5 env var values per line, got: {line!r}"
        jid_val, sid_val, nnodes_val, ntasks_val, procid_val = parts
        assert jid_val == str(job_id), f"SLURM_JOB_ID wrong: {jid_val!r}"
        assert sid_val == "0", f"SLURM_STEPID wrong: {sid_val!r}"
        assert nnodes_val == "2", f"SLURM_NNODES wrong: {nnodes_val!r}"
        assert ntasks_val == str(num_tasks), f"SLURM_NTASKS wrong: {ntasks_val!r}"
        procids.add(procid_val)

    assert procids == {
        "0",
        "1",
    }, f"SLURM_PROCID values should be {{0,1}}; got {procids!r}"


def test_async_export_none():
    """--export=NONE suppresses user environment variables in an async step.

    A variable set in the batch script's environment must not appear in the
    async step's environment when --export=NONE is used.
    """
    sentinel = "ASYNC_EXPORT_TEST_VAR"
    out_file = "export_none.out"

    script = "export_none.sh"
    atf.make_bash_script(
        script,
        f"""export {sentinel}=should_not_appear
srun --async --export=NONE -o {out_file} \\
    /bin/bash -c 'echo val=${{{sentinel}:-unset}}'
swait
""",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t1 {script}", fatal=True)
    assert atf.wait_for_step_accounted(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.wait_for_file(out_file, fatal=True)
    output = atf.run_command_output(f"cat {out_file}", fatal=True).strip()
    assert (
        output == "val=unset"
    ), f"--export=NONE should suppress {sentinel}; got {output!r}"


def test_async_export_specific_var():
    """--export=VAR=val passes a specific variable into an async step.

    Covers the targeted export form: the named variable must appear in
    the step environment even if it was not in the batch script's env.
    """
    sentinel = "ASYNC_EXPORT_SPECIFIC_VAR"
    value = "hello_from_export"
    out_file = "export_specific.out"

    script = "export_specific.sh"
    atf.make_bash_script(
        script,
        f"srun --async --export={sentinel}={value} -o {out_file} "
        f"/bin/bash -c 'echo ${{{sentinel}:-unset}}'\nswait\n",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t1 {script}", fatal=True)
    assert atf.wait_for_step_accounted(
        job_id, 0, fatal=True
    ), f"async step {job_id}.0 should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.wait_for_file(out_file, fatal=True)
    output = atf.run_command_output(f"cat {out_file}", fatal=True).strip()
    assert (
        output == value
    ), f"--export={sentinel}={value} should set the variable; got {output!r}"
