############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify the <exit>:<signal> exit code rendering shared by the client commands.

scontrol, squeue, sacct, sview and the jobcomp plugins all decode a
wait()-style exit code through one helper. The helper decides *which* of the
two numbers is populated, so this pins both halves black-box: a job that exits
non-zero must render <status>:0, and a signal-killed job must render 0:<signal>.
DerivedExitCode goes through the same helper and is pinned here too.
"""

import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    # The per-step sacct rows asserted below do not exist under nosteps
    # (nor does the batch row under nojobs), as the retired test7.13 knew.
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nosteps")
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nojobs")
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_slurm_running()


# Raised over atf's 45s default: every test here waits for a job to reach
# DONE, which on a loaded runner includes the whole epilog.
DONE_WAIT_SECS = 120


def _scontrol_exit_code(job_id, field="ExitCode"):
    """<exit>:<sig> as scontrol show job renders the named field.

    -d because DerivedExitCode is a detail-only field; ExitCode prints
    either way, so one command covers both.
    """

    output = atf.run_command_output(f"scontrol -d show job {job_id}", fatal=True)
    match = re.search(rf"\b{field}=(\d+):(\d+)", output)
    assert match, f"no {field} in scontrol output for job {job_id}: {output!r}"
    return match.group(1), match.group(2)


def _stepmgr_enabled():
    """A stepmgr job does not track DerivedExitCode on the controller, so
    scontrol has nothing to render for it."""

    params = atf.get_config_parameter("SlurmctldParameters", default="", quiet=True)
    return "enable_stepmgr" in (params or "")


def test_exit_status_rendering():
    """A job exiting non-zero reports <status>:0, not the signal half."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=1:00 --job-name=test_108_13_status --wrap 'exit 7'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", timeout=DONE_WAIT_SECS, fatal=True)

    exit_status, term_sig = _scontrol_exit_code(job_id)
    assert exit_status == "7", f"scontrol ExitCode status half: {exit_status}"
    assert term_sig == "0", f"an exited job carries no signal; got {term_sig}"

    atf.wait_for_job_accounted(job_id, "ExitCode", r"^7:0$", fatal=True)
    out = atf.run_command_output(
        f"sacct -j {job_id} -X -n -P -o ExitCode", fatal=True
    ).strip()
    assert out == "7:0", f"sacct ExitCode: {out!r}"


@pytest.mark.parametrize(
    "wrap,expected",
    [
        ("exit 255", "255:0"),
        # sbatch --wrap generates a #!/bin/sh script, and dash rejects a
        # negative argument to exit, so spell this one out with bash.
        ('/bin/bash -c "exit -1"', "255:0"),
    ],
)
def test_exit_status_clamped_to_byte(wrap, expected):
    """job_exit_code.shtml: the exit code "is an 8 bit unsigned number
    ranging between 0 and 255", and a negative one is displayed as its
    unsigned value in that range. Pin the top of the range and the wrap,
    which is where a sign-extension or width regression in the shared
    decode helper would show up.
    """

    job_id = atf.submit_job_sbatch(
        f"-N1 --time=1:00 --job-name=test_108_13_clamp --wrap '{wrap}'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", timeout=DONE_WAIT_SECS, fatal=True)

    exit_status, term_sig = _scontrol_exit_code(job_id)
    assert (
        f"{exit_status}:{term_sig}" == expected
    ), f"scontrol ExitCode: {exit_status}:{term_sig}, expected {expected}"

    atf.wait_for_job_accounted(
        job_id, "ExitCode", rf"^{re.escape(expected)}$", fatal=True
    )
    out = atf.run_command_output(
        f"sacct -j {job_id} -X -n -P -o ExitCode", fatal=True
    ).strip()
    assert out == expected, f"sacct ExitCode: {out!r}, expected {expected!r}"


def test_signal_rendering():
    """A signal-killed job reports 0:<signal>, not the status half.

    The batch shell signals itself so the step's real wait() status reaches
    both slurmctld and accounting; cancelling instead would log completion
    before the step's exit code is known.
    """

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_108_13_signal --wrap 'kill -KILL $$'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", timeout=DONE_WAIT_SECS, fatal=True)

    exit_status, term_sig = _scontrol_exit_code(job_id)
    assert exit_status == "0", f"a signaled job carries no status; got {exit_status}"
    assert term_sig == "9", f"expected SIGKILL in the signal half; got {term_sig}"

    atf.wait_for_job_accounted(job_id, "ExitCode", r"^0:9$", fatal=True)
    out = atf.run_command_output(
        f"sacct -j {job_id} -X -n -P -o ExitCode", fatal=True
    ).strip()
    assert out == "0:9", f"sacct ExitCode: {out!r}"


@pytest.mark.parametrize(
    "wrap,expected",
    [("exit 7", "7:0"), ("kill -KILL $$", "0:9")],
)
def test_squeue_exit_code_rendering(wrap, expected):
    """squeue -O exit_code renders the same <status>:<signal> split.

    squeue decodes through the same helper as scontrol and sacct but is
    otherwise untested, so pin both halves here too.
    """

    job_id = atf.submit_job_sbatch(
        f"-N1 --time=5:00 --job-name=test_108_13_squeue --wrap '{wrap}'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", timeout=DONE_WAIT_SECS, fatal=True)

    out = atf.run_command_output(
        f"squeue --jobs={job_id} --states=all -h -O exit_code", fatal=True
    ).strip()
    assert out == expected, f"squeue exit_code: {out!r}, expected {expected!r}"


@pytest.mark.parametrize(
    "steps,job_ec,derived_ec,step_ecs",
    [
        # A failing step under a script that exits 0: the step's code
        # reaches DerivedExitCode only, never the job's ExitCode.
        ('srun -n1 /bin/sh -c "exit 123"; exit 0', "0:0", "123:0", ["123:0"]),
        # The mirror image: the script fails while every step succeeds, so
        # DerivedExitCode stays 0:0 and only ExitCode carries the failure.
        (
            "srun -n1 /bin/true; srun -n1 /bin/true; exit 33",
            "33:0",
            "0:0",
            ["0:0", "0:0"],
        ),
        # sacct(1): DerivedExitCode is "the highest exit code returned by
        # the job's job steps". Put the highest one first, so an
        # implementation that reported the last step's code cannot pass by
        # coincidence.
        (
            'srun -n1 /bin/sh -c "exit 123"; srun -n1 /bin/sh -c "exit 5"; exit 0',
            "0:0",
            "123:0",
            ["123:0", "5:0"],
        ),
        # The mirror image: the highest code last. This is the ordering the
        # retired test7.13 ran and the one the running-maximum update in the
        # stepmgr is defined by; without it an implementation reporting the
        # first (or first non-zero) step's code passes every tuple above.
        (
            'srun -n1 /bin/sh -c "exit 5"; srun -n1 /bin/sh -c "exit 123"; exit 0',
            "0:0",
            "123:0",
            ["5:0", "123:0"],
        ),
    ],
)
def test_derived_exit_code_rendering(steps, job_ec, derived_ec, step_ecs):
    """DerivedExitCode reports the highest step exit code, not the job's.

    Both fields go through the same decode helper, so pin them against each
    other in both directions, in scontrol and in accounting, down to the
    per-step rows.
    """

    job_id = atf.submit_job_sbatch(
        f"-N1 --time=5:00 --job-name=test_108_13_derived --wrap '{steps}'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", timeout=DONE_WAIT_SECS, fatal=True)

    exit_status, term_sig = _scontrol_exit_code(job_id)
    assert (
        f"{exit_status}:{term_sig}" == job_ec
    ), f"scontrol ExitCode: {exit_status}:{term_sig}"
    if not _stepmgr_enabled():
        exit_status, term_sig = _scontrol_exit_code(job_id, "DerivedExitCode")
        assert (
            f"{exit_status}:{term_sig}" == derived_ec
        ), f"scontrol DerivedExitCode: {exit_status}:{term_sig}"

    # Sync on the accounting State first: DerivedExitCode == "0:0" is also
    # the pre-completion default, so for a tuple expecting 0:0 that wait
    # could pass while the slurmdbd write is still in flight. A terminal
    # State never appears before the completion record is applied.
    atf.wait_for_job_accounted(job_id, "State", r"^(COMPLETED|FAILED)$", fatal=True)
    atf.wait_for_job_accounted(
        job_id, "DerivedExitCode", rf"^{re.escape(derived_ec)}$", fatal=True
    )
    out = atf.run_command_output(
        f"sacct -j {job_id} -X -n -P -o ExitCode,DerivedExitCode", fatal=True
    ).strip()
    assert out == f"{job_ec}|{derived_ec}", f"sacct ExitCode|DerivedExitCode: {out!r}"

    # The per-step rows carry each step's own code: the batch step mirrors
    # the script's, and each srun step its own.
    rows = atf.run_command_output(
        f"sacct -j {job_id} -n -P -o JobID,ExitCode", fatal=True
    ).strip()
    step_codes = dict(line.split("|") for line in rows.splitlines() if "|" in line)
    assert step_codes.get(f"{job_id}.batch") == job_ec, f"batch step ExitCode: {rows!r}"
    for index, expected in enumerate(step_ecs):
        assert (
            step_codes.get(f"{job_id}.{index}") == expected
        ), f"step {index} ExitCode: {rows!r}"
