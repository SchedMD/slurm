############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify swait's --json/--yaml serialization and data_parser selection.

Covers the field-level content of a step completion in both formats, the
--json/--yaml data_parser argument forms (list, invalid, too old to carry
step completions, an explicit plugin name, and a +params suffix), the
mutually-exclusive usage error, and -Q suppressing the completion line in
every output format.

Assertions here are about field content and parser selection, not about
the shape of a mode's stream.
"""

import json
import re

import pytest
import yaml

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Issue 50928: per-step SRUN_STEPS_DRAINED dispatch lives in slurmd 26.11",
    )
    atf.require_tool("swait")
    atf.require_version(
        (26, 11),
        component="bin/swait",
        reason="Issue 50928: swait --follow/--json were added in 26.11",
    )
    # Every job here is a single -N1 step; the 2 nodes and 4 CPUs are the shape
    # the whole test_162_5..8 family requires, kept identical here so the suite
    # does not reconfigure between them.
    atf.require_nodes(2, [("CPUs", 4)])
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


# A job id far above any the controller will hand out, so a target
# built from it always reaches the controller and always fails there.
BOGUS_JOBID = 4294967292

# Raised over atf's 45s default: the test_162_5..8 family parks several
# concurrent steps, so a step can take longer than that to appear.
STEP_WAIT_SECS = 60

# Long enough that swait subscribes while the step is still running.
# wait_for_step() (1s poll) plus the stepmgr lookup (a scontrol
# round-trip) can consume several seconds before swait even starts;
# give generous headroom.
STEP_SECS = 15
# Only has to separate a real wait from a fast return (~0.2s), so keep it far
# below STEP_SECS; subscribe setup eats an unpredictable slice of the step.
FLOOR_SECS = 1
CEILING_SECS = STEP_SECS + 8

# run_command timeout for a swait that blocks on a step: STEP_SECS plus job
# startup and the subscribe round trip, with headroom for a loaded runner.
# Raised over atf's 60s default so an outer kill cannot mask a real wait.
BLOCKING_CMD_SECS = 180


def _listed_plugins(stdout):
    return [
        line.strip()
        for line in stdout.splitlines()
        if line.strip().startswith("data_parser/")
    ]


def _data_parser_plugins():
    """Plugin names swait --json=list reports, for tests that need a valid
    --json=<data_parser> argument rather than testing the listing itself."""
    result = atf.run_command("swait --json=list", fatal=True)
    plugins = _listed_plugins(result["stdout"])
    assert plugins, f"no data_parser plugins listed: {result['stdout']!r}"
    return plugins


def _newest_data_parser_plugin():
    """Newest name from --json=list, which is the documented way to discover a
    valid --json=<data_parser> argument. Only the newest plugin knows the
    step-completion type, and listing order is unspecified."""
    return max(
        _data_parser_plugins(),
        key=lambda name: [int(n) for n in re.findall(r"\d+", name)],
    )


def test_step_wait_json():
    """--json emits one JSON object: the step_id object, a decoded exit_code
    (status/return_code), and a scalar state string (no JOB_COMPLETING)."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_8_json "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --json {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    obj = json.loads(result["stdout"].strip())
    # data_parser SLURM_STEP_ID dumps step_id as a string ("0"), job_id as a
    # UINT32_NO_VAL, and PROCESS_EXIT_CODE status as a flag.
    assert (
        atf.get_data_parser_number(obj["step_id"]["job_id"]) == job_id
    ), f"bad json: {result['stdout']!r}"
    assert obj["step_id"]["step_id"] == "0", f"bad json: {result['stdout']!r}"
    # swait.1 names step_het_component as the discriminator: its set field is
    # false for a step that is not a het step component.
    assert (
        atf.get_data_parser_number(obj["step_id"]["step_het_component"]) is None
    ), f"a non-het step must report step_het_component unset: {result['stdout']!r}"
    assert (
        atf.get_data_parser_flag(obj["exit_code"]["status"]) == "SUCCESS"
    ), f"bad json: {result['stdout']!r}"
    assert (
        atf.get_data_parser_number(obj["exit_code"]["return_code"]) == 0
    ), f"bad json: {result['stdout']!r}"
    # state is the terminal base state, COMPLETING masked.
    assert (
        atf.get_data_parser_flag(obj["state"]) == "COMPLETED"
    ), f"bad json: {result['stdout']!r}"
    # Each completion is emitted bare, not wrapped in a response object.
    assert not (
        {"meta", "errors", "warnings"} & obj.keys()
    ), f"completion must carry no response wrapper: {result['stdout']!r}"


def test_step_wait_json_nonzero():
    """--json renders a non-zero exit via the data_parser exit_code object:
    status ERROR carrying the numeric return_code."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_8_json_rc "
        f"--wrap 'srun -n1 sh -c \"sleep {STEP_SECS}; exit 7\"'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --json {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    obj = json.loads(result["stdout"].strip())
    assert obj["step_id"]["step_id"] == "0", f"bad json: {result['stdout']!r}"
    assert (
        atf.get_data_parser_flag(obj["exit_code"]["status"]) == "ERROR"
    ), f"bad json: {result['stdout']!r}"
    assert (
        atf.get_data_parser_number(obj["exit_code"]["return_code"]) == 7
    ), f"bad json: {result['stdout']!r}"


def test_step_wait_yaml():
    """--yaml emits the same notification body serialized as YAML, with the
    same field-level content --json does (not just crash-free substrings)."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_8_yaml "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --yaml {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    obj = yaml.safe_load(result["stdout"])
    assert (
        atf.get_data_parser_number(obj["step_id"]["job_id"]) == job_id
    ), f"bad yaml: {result['stdout']!r}"
    assert obj["step_id"]["step_id"] == "0", f"bad yaml: {result['stdout']!r}"
    assert (
        atf.get_data_parser_flag(obj["exit_code"]["status"]) == "SUCCESS"
    ), f"bad yaml: {result['stdout']!r}"
    assert (
        atf.get_data_parser_number(obj["exit_code"]["return_code"]) == 0
    ), f"bad yaml: {result['stdout']!r}"
    assert (
        atf.get_data_parser_flag(obj["state"]) == "COMPLETED"
    ), f"bad yaml: {result['stdout']!r}"


def test_json_yaml_mutually_exclusive():
    """--json and --yaml together are a usage error (exit 1)."""

    result = atf.run_command(
        "swait --json --yaml 1",
        env_vars="SLURM_STEPMGR=localhost",
        xfail=True,
    )
    assert result["exit_code"] == 1, f"swait exited {result['exit_code']}, expected 1"
    assert "mutually exclusive" in result["stderr"], f"got: {result['stderr']!r}"


def _assert_list_stream_split(result):
    """swait.1: =list prints the plugins to stdout "with a header line on
    stderr". Keeping the header off stdout is what makes the output
    pipeable, so pin the split, not just the content."""

    assert result["stderr"].strip(), f"expected a header on stderr: {result!r}"
    stray = [
        line
        for line in result["stdout"].splitlines()
        if line.strip() and not line.strip().startswith("data_parser/")
    ]
    assert not stray, f"non-plugin lines leaked onto stdout: {stray!r}"


def test_json_list_data_parsers():
    """--json=list prints the data_parser plugins and exits 0, without
    needing a resolvable job target."""

    result = atf.run_command("swait --json=list")
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    assert _listed_plugins(result["stdout"]), f"none listed: {result['stdout']!r}"
    _assert_list_stream_split(result)


def test_yaml_list_data_parsers():
    """--yaml=list lists the same plugins as --json=list."""

    yaml_result = atf.run_command("swait --yaml=list")
    assert yaml_result["exit_code"] == 0, f"swait exited {yaml_result['exit_code']}"
    assert set(_listed_plugins(yaml_result["stdout"])) == set(
        _data_parser_plugins()
    ), f"--yaml=list differs from --json=list: {yaml_result['stdout']!r}"
    _assert_list_stream_split(yaml_result)


def test_json_invalid_data_parser():
    """An unknown --json=<data_parser> exits 1 (swait's error code) rather
    than 2, which swait reserves for an expired --timeout. The rejection is
    at plugin load time, before the job is looked up."""

    result = atf.run_command("swait --json=data_parser/v0.0.0 1", xfail=True)
    assert result["exit_code"] == 1, f"swait exited {result['exit_code']}, expected 1"
    assert "data_parser/v0.0.0" in result["stderr"], (
        f"expected plugin name in stderr (confirming plugin-load rejection, "
        f"not an unrelated error): {result['stderr']!r}"
    )
    assert (
        "Invalid job id" not in result["stderr"]
    ), f"controller was consulted before plugin load failed: {result['stderr']!r}"


def test_json_unsupported_data_parser_rejected():
    """A data_parser too old to carry step completions is rejected up front
    with exit 1, before the job is looked up; at least one listed plugin is
    accepted."""

    listed = _data_parser_plugins()
    accepted = []
    for plugin in listed:
        # No env fast path and a job id that cannot exist: an accepted
        # plugin gets as far as the controller and fails on the job, so a
        # rejection that never names the job proves the plugin check runs
        # first. Pinning that ordering is the point of the test.
        result = atf.run_command(
            f"env -u SLURM_STEPMGR -u SLURM_JOB_ID -u SLURM_JOB_SLUID "
            f"swait --json={plugin} {BOGUS_JOBID}",
            xfail=True,
        )
        if "cannot print step completions" in result["stderr"]:
            assert (
                result["exit_code"] == 1
            ), f"{plugin} exited {result['exit_code']}, expected 1"
            assert str(BOGUS_JOBID) not in result["stderr"], (
                f"{plugin}: the controller was consulted before the plugin "
                f"was rejected: {result['stderr']!r}"
            )
        else:
            accepted.append(plugin)
    assert accepted, f"no plugin accepted step-completion output: {listed}"
    # Deliberately no assertion that some plugin was rejected. Which shipped
    # parsers are too old to carry a step completion is an inventory fact
    # that changes every release; the per-plugin assertions in the loop above
    # are what actually pin the up-front rejection.


def test_step_wait_json_explicit_data_parser():
    """--json=<data_parser> dumps the same completion fields as a bare
    --json, proving the selected plugin is what serializes the body."""

    # Use a name --json=list actually reports: that is the documented way a
    # user discovers a valid argument.
    plugin = _newest_data_parser_plugin()

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_8_json_parser "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --json={plugin} {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    obj = json.loads(result["stdout"].strip())
    assert (
        atf.get_data_parser_number(obj["step_id"]["job_id"]) == job_id
    ), f"bad json: {result['stdout']!r}"
    assert obj["step_id"]["step_id"] == "0", f"bad json: {result['stdout']!r}"
    assert (
        atf.get_data_parser_flag(obj["exit_code"]["status"]) == "SUCCESS"
    ), f"bad json: {result['stdout']!r}"
    assert (
        atf.get_data_parser_flag(obj["state"]) == "COMPLETED"
    ), f"bad json: {result['stdout']!r}"


def test_step_wait_json_data_parser_params():
    """A +params suffix reaches the plugin: +complex renders UINT32_NO_VAL
    fields as bare numbers instead of {set,infinite,number} objects."""

    plugin = _newest_data_parser_plugin()

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_8_json_complex "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --json={plugin}+complex {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    obj = json.loads(result["stdout"].strip())
    assert (
        obj["step_id"]["job_id"] == job_id
    ), f"complex mode should emit a bare job_id: {result['stdout']!r}"


def test_step_wait_yaml_explicit_data_parser():
    """--yaml=<data_parser> takes the same argument forms --json does, so an
    explicit plugin name selects the serializer for YAML too."""

    plugin = _newest_data_parser_plugin()

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_8_yaml_parser "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --yaml={plugin} {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    obj = yaml.safe_load(result["stdout"])
    assert (
        atf.get_data_parser_number(obj["step_id"]["job_id"]) == job_id
    ), f"bad yaml: {result['stdout']!r}"
    assert obj["step_id"]["step_id"] == "0", f"bad yaml: {result['stdout']!r}"
    assert (
        atf.get_data_parser_flag(obj["state"]) == "COMPLETED"
    ), f"bad yaml: {result['stdout']!r}"


@pytest.mark.parametrize("fmt", ["", "--json", "--yaml"])
def test_quiet_suppresses_output(fmt):
    """-Q suppresses the stdout completion line (human or serialized) while
    preserving the wait outcome (rc 0)."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_8_quiet "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait -Q {fmt} {job_id}.0".strip(),
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    assert (
        FLOOR_SECS <= result["duration"] < CEILING_SECS
    ), f"swait took {result['duration']:.1f}s; expected to wait for step 0 end, not fast-return"
    assert (
        result["stdout"] == ""
    ), f"-Q should suppress completion output ({fmt!r}); stdout: {result['stdout']!r}"
    # swait.1 -Q: "Suppress informational messages on stderr and
    # step-completion output on stdout." Nothing here is an error, so the
    # stderr half must be silent too.
    assert (
        result["stderr"] == ""
    ), f"-Q should suppress info messages ({fmt!r}); stderr: {result['stderr']!r}"
