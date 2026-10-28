############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the --layout option and where a job's layout shows up.

A layout is a comma-separated list of key=value pairs that Slurm passes on
without interpreting, for a site's job_submit and switch plugins and its
prologs and epilogs. These cases check that it is refused when it is not that
shape, that it reaches every place it is documented to reach, and that it can
only be changed while the job is pending.
"""

import re
import shlex

import pytest

import atf

LAYOUT = "tpu7x=4x4x4,foo=bar"

# Only whitespace and control characters are refused, so a value may hold
# characters a shell or SQL treats specially; they must come back unchanged.
SPECIAL_LAYOUT = "q='a\"b`c\\d,s=$HOME;a|b&c>d<e(f)*?[g]"

# Each script writes the SLURM_JOB_LAYOUT it was given, or "unset" when it was
# not given one, so an empty layout can be told apart from none.
SCRIPTS = ["Prolog", "Epilog", "PrologSlurmctld", "EpilogSlurmctld"]
# MailProg records the same way, but only runs for a job that asks for mail.
# TaskProlog runs for every task, as the job's user.
RECORDERS = SCRIPTS + ["MailProg", "TaskProlog"]

# slurmctld refuses a longer layout; the clients do not check the length.
MAX_LAYOUT_LEN = 1024

requires_slurmd = pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 11),
    reason="Issue 51191: the job credential carries the layout from 26.11",
)
CLIENTS = [
    pytest.param(
        command,
        marks=pytest.mark.skipif(
            atf.get_version(f"bin/{command}") < (26, 11),
            reason=f"Issue 51191: {command} --layout was added in 26.11",
        ),
    )
    for command in ["srun", "salloc"]
]
requires_sacct = pytest.mark.skipif(
    atf.get_version("bin/sacct") < (26, 11),
    reason="Issue 51191: sacct shows a job's layout from 26.11",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="Issue 51191: --layout added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Issue 51191: scontrol update Layout added in 26.11",
    )
    atf.require_accounting()

    # Absolute, since slurmd and slurmctld run the scripts from their own
    # directories.
    for script in RECORDERS:
        path = f"{atf.module_tmp_path}/{script}.sh"
        atf.make_bash_script(
            path,
            f'echo "${{SLURM_JOB_LAYOUT-unset}}" > '
            f'{script_out(script, "$SLURM_JOB_ID")}\n',
        )
        atf.set_config_parameter(script, path)

    atf.require_slurm_running()


def script_out(script, job_id):
    return f"{atf.module_tmp_path}/{script}.{job_id}"


def assert_scripts_saw(job_id, scripts, expected):
    for script in scripts:
        atf.assert_file_contents(
            script_out(script, job_id),
            expected,
            message=f"{script} should see SLURM_JOB_LAYOUT as {expected}",
        )


def sacct_layout(job_id):
    """The Layout sacct has for the job row once the job has completed."""

    atf.wait_for_job_accounted(job_id, "State", "COMPLETED", fatal=True)
    return atf.run_command_output(
        f"sacct -nPX -j {job_id} -o Layout", fatal=True
    ).strip()


@pytest.mark.parametrize(
    "layout",
    [
        "tpu7x",
        "a=1,,b=2",
        "=v",
        "k=",
        "a=1,",
        "k=a b",
        "k=a\tb",
        "k=a\nb",
        "k=a\x01b",
        "k=a\x7fb",
    ],
)
def test_malformed_layout_refused(layout):
    """sbatch refuses a layout that is not plain key=value pairs."""

    # No fatal=True: the refusal is what is being asserted. Quoted for the
    # shell, so the layout reaches sbatch intact.
    result = atf.run_command(f"sbatch --layout={shlex.quote(layout)} --wrap true")
    assert result["exit_code"] != 0, (
        f"sbatch should refuse --layout={layout!r}, got exit_code "
        f"{result['exit_code']} and stdout: {result['stdout']}"
    )
    assert (
        "Invalid --layout" in result["stderr"]
    ), f"The refusal should name --layout, got: {result['stderr']}"


@pytest.mark.parametrize("command", CLIENTS)
def test_malformed_layout_refused_srun_salloc(command):
    """srun and salloc refuse a malformed layout the way sbatch does."""

    # No fatal=True: the refusal is what is being asserted.
    result = atf.run_command(f"{command} --layout=tpu7x true")
    assert result["exit_code"] != 0, f"{command} should refuse --layout=tpu7x"
    assert (
        "Invalid --layout" in result["stderr"]
    ), f"The refusal should name --layout, got: {result['stderr']}"


@requires_sacct
def test_layout_special_characters_kept():
    """Characters a shell or SQL treats specially are kept as given."""

    job_id = atf.submit_job_sbatch(
        f"--layout={shlex.quote(SPECIAL_LAYOUT)} --hold --wrap true", fatal=True
    )

    assert (
        atf.get_job_parameter(job_id, "Layout") == SPECIAL_LAYOUT
    ), f"scontrol show job should show Layout={SPECIAL_LAYOUT}"
    assert atf.wait_for_job_accounted(
        job_id, "Layout", f"^{re.escape(SPECIAL_LAYOUT)}$"
    ), f"sacct should show the layout {SPECIAL_LAYOUT} unchanged"


@pytest.mark.skipif(
    atf.get_version("bin/squeue") < (26, 11),
    reason="Issue 51191: squeue -O Layout was added in 26.11",
)
def test_layout_shown():
    """A job's layout is shown by scontrol and squeue."""

    job_id = atf.submit_job_sbatch(f"--layout={LAYOUT} --hold --wrap true", fatal=True)

    assert (
        atf.get_job_parameter(job_id, "Layout") == LAYOUT
    ), f"scontrol show job should show Layout={LAYOUT}"

    output = atf.run_command_output(
        f"squeue -h -j {job_id} -O Layout:64", fatal=True
    ).strip()
    assert output == LAYOUT, f"squeue -O Layout should show {LAYOUT}, got: {output}"


@requires_slurmd
def test_empty_layout_is_none():
    """An empty --layout= gives the job no layout, not an empty one."""

    job_id = atf.submit_job_sbatch("--layout= --wrap true", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    assert (
        atf.get_job_parameter(job_id, "Layout") is None
    ), "scontrol show job should not show a Layout for --layout="
    assert_scripts_saw(job_id, SCRIPTS + ["TaskProlog"], "unset")


def test_layout_update_pending():
    """A pending job's layout can be changed, and cleared with an empty value."""

    job_id = atf.submit_job_sbatch(f"--layout={LAYOUT} --hold --wrap true", fatal=True)

    atf.run_command(f"scontrol update job={job_id} layout=tpu7x=2x2x2", fatal=True)
    assert (
        atf.get_job_parameter(job_id, "Layout") == "tpu7x=2x2x2"
    ), "scontrol update should change a pending job's layout"

    atf.run_command(f"scontrol update job={job_id} layout=", fatal=True)
    assert (
        atf.get_job_parameter(job_id, "Layout") is None
    ), "An empty layout should clear a pending job's layout"


@pytest.mark.parametrize(
    "layout",
    ["tpu7x", "k=", "k=a b"],
)
def test_malformed_layout_update_refused(layout):
    """slurmctld refuses a malformed layout that did not come through sbatch.

    scontrol update sends the layout straight to slurmctld without the check
    sbatch makes, so this is the check slurmctld makes itself.
    """

    job_id = atf.submit_job_sbatch(f"--layout={LAYOUT} --hold --wrap true", fatal=True)

    # No fatal=True: the refusal is what is being asserted. Quoted for the
    # shell, so the layout reaches scontrol as one argument.
    result = atf.run_command(
        f"scontrol update job={job_id} {shlex.quote(f'layout={layout}')}"
    )
    assert result["exit_code"] != 0, f"slurmctld should refuse layout={layout!r}"
    assert (
        "Invalid layout" in result["stderr"]
    ), f"The refusal should say the layout is invalid, got: {result['stderr']}"
    assert (
        atf.get_job_parameter(job_id, "Layout") == LAYOUT
    ), "A refused update should leave the layout as it was"


def test_layout_length_limit():
    """slurmctld takes a layout up to its length limit and refuses a longer one.

    The clients do not check the length, so the refusals come from slurmctld.
    """

    longest = "k=" + "v" * (MAX_LAYOUT_LEN - 2)
    too_long = longest + "v"

    job_id = atf.submit_job_sbatch(f"--layout={longest} --hold --wrap true", fatal=True)
    assert (
        atf.get_job_parameter(job_id, "Layout") == longest
    ), f"A layout of {MAX_LAYOUT_LEN} bytes should be kept whole"

    # No fatal=True: the refusals are what is being asserted.
    result = atf.run_command(f"sbatch --layout={too_long} --hold --wrap true")
    assert result["exit_code"] != 0, (
        f"slurmctld should refuse a layout of {MAX_LAYOUT_LEN + 1} bytes, got "
        f"stdout: {result['stdout']}"
    )
    assert (
        "parameter too long" in result["stderr"]
    ), f"The refusal should say the layout is too long, got: {result['stderr']}"

    job_id = atf.submit_job_sbatch(f"--layout={LAYOUT} --hold --wrap true", fatal=True)
    result = atf.run_command(f"scontrol update job={job_id} layout={too_long}")
    assert result["exit_code"] != 0, (
        f"slurmctld should refuse an update to a layout of {MAX_LAYOUT_LEN + 1} "
        "bytes"
    )
    assert (
        "parameter too long" in result["stderr"]
    ), f"The refusal should say the layout is too long, got: {result['stderr']}"
    assert (
        atf.get_job_parameter(job_id, "Layout") == LAYOUT
    ), "A refused update should leave the layout as it was"


def test_layout_update_running_refused():
    """A running job's layout cannot be changed, since it already went out."""

    # Cancelled by submit_job_sbatch() when the test ends.
    job_id = atf.submit_job_sbatch(
        f"--layout={LAYOUT} --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # No fatal=True: the refusal is what is being asserted.
    result = atf.run_command(f"scontrol update job={job_id} layout=tpu7x=2x2x2")
    assert result["exit_code"] != 0, "Changing a running job's layout should be refused"
    assert (
        "no longer pending" in result["stderr"]
    ), f"The refusal should say the job is not pending, got: {result['stderr']}"
    assert (
        atf.get_job_parameter(job_id, "Layout") == LAYOUT
    ), "A refused update should leave the layout as it was"


@requires_slurmd
def test_layout_in_scripts():
    """Prolog, Epilog, PrologSlurmctld and EpilogSlurmctld get the layout."""

    job_id = atf.submit_job_sbatch(f"--layout={LAYOUT} --wrap true", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    assert_scripts_saw(job_id, SCRIPTS, LAYOUT)


@requires_slurmd
def test_layout_in_job_env():
    """The batch script, its steps and the TaskProlog get the layout."""

    script = "job_env.sh"
    atf.make_bash_script(
        script,
        f'echo "${{SLURM_JOB_LAYOUT-unset}}" > '
        f'{script_out("Batch", "$SLURM_JOB_ID")}\n'
        # Dropped from what srun inherits, so the step can only get it from
        # its own credential.
        "env -u SLURM_JOB_LAYOUT srun sh -c "
        f'\'echo "${{SLURM_JOB_LAYOUT-unset}}" > '
        f"{script_out('Step', '$SLURM_JOB_ID')}'\n",
    )
    job_id = atf.submit_job_sbatch(
        f"--layout={LAYOUT} --output=/dev/null {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    assert_scripts_saw(job_id, ["Batch", "Step", "TaskProlog"], LAYOUT)


def test_layout_in_mail():
    """MailProg gets the layout of the job it is sending mail about."""

    job_id = atf.submit_job_sbatch(
        f"--layout={LAYOUT} --mail-type=END --wrap true", fatal=True
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    assert_scripts_saw(job_id, ["MailProg"], LAYOUT)


@requires_slurmd
@pytest.mark.parametrize("command", CLIENTS)
def test_layout_srun_salloc(command):
    """srun and salloc give the job the layout the way sbatch does."""

    if command == "srun":
        job_id = atf.submit_job_srun(f"--layout={LAYOUT} true", fatal=True)
    else:
        # A step, so the node Prolog runs without PrologFlags=Alloc.
        job_id = atf.submit_job_salloc(f"--layout={LAYOUT} srun true", fatal=True)

    assert (
        atf.get_job_parameter(job_id, "Layout") == LAYOUT
    ), f"A {command} job should show Layout={LAYOUT}"
    assert_scripts_saw(job_id, SCRIPTS, LAYOUT)


@requires_sacct
def test_layout_in_accounting():
    """A job's layout is shown by sacct on the job row, not on step rows."""

    job_id = atf.submit_job_sbatch(f"--layout={LAYOUT} --wrap 'srun true'", fatal=True)

    assert (
        sacct_layout(job_id) == LAYOUT
    ), f"sacct should show the job's layout {LAYOUT}"

    # Step rows keep showing the task distribution in the same field.
    rows = atf.run_command_output(
        f"sacct -nP -j {job_id} -o JobID,Layout", fatal=True
    ).splitlines()
    steps = [row.split("|") for row in rows if "." in row.split("|")[0]]
    assert steps, f"sacct should list the job's steps, got: {rows}"
    for step_id, layout in steps:
        assert re.fullmatch(r"[A-Za-z]+(,[A-Za-z]+)*", layout) and layout != LAYOUT, (
            f"Step {step_id} should show its task distribution, not the job "
            f"layout, got: {layout!r}"
        )


@requires_sacct
@requires_slurmd
def test_layout_update_reaches_job():
    """A layout changed while pending reaches accounting, then the scripts."""

    job_id = atf.submit_job_sbatch(f"--layout={LAYOUT} --hold --wrap true", fatal=True)
    # Wait for the job to be recorded, so the update has a row to change.
    atf.wait_for_job_accounted(job_id, "Layout", LAYOUT, fatal=True)

    atf.run_command(f"scontrol update job={job_id} layout=tpu7x=2x2x2", fatal=True)
    assert atf.wait_for_job_accounted(
        job_id, "Layout", "^tpu7x=2x2x2$"
    ), "sacct should show a pending job's changed layout before it runs"

    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    assert_scripts_saw(job_id, SCRIPTS, "tpu7x=2x2x2")
    assert (
        sacct_layout(job_id) == "tpu7x=2x2x2"
    ), "sacct should keep the changed layout once the job has run"


@requires_sacct
@requires_slurmd
def test_layout_clear_reaches_job():
    """A layout cleared while pending is cleared in accounting too."""

    job_id = atf.submit_job_sbatch(f"--layout={LAYOUT} --hold --wrap true", fatal=True)
    # Wait for the job to be recorded, so the clear has a row to change.
    atf.wait_for_job_accounted(job_id, "Layout", LAYOUT, fatal=True)

    atf.run_command(f"scontrol update job={job_id} layout=", fatal=True)
    assert atf.wait_for_job_accounted(
        job_id, "Layout", "^$"
    ), "sacct should show a pending job's cleared layout before it runs"

    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    assert_scripts_saw(job_id, SCRIPTS, "unset")
    layout = sacct_layout(job_id)
    assert layout == "", f"sacct should show no layout once cleared, got: {layout}"
