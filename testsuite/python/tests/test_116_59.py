############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the --runtime job submission option and its runtime plugins."""

import json

import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/srun",
        reason="Issue 50190: --runtime plugin ownership added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/salloc",
        reason="Issue 50190: --runtime plugin ownership added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="Issue 50190: --runtime plugin ownership added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Issue 50190: --runtime plugin ownership added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/squeue",
        reason="Issue 50190: squeue -O Runtime added in 26.11",
    )
    # salloc rejects --container without an interactive step to pass it to.
    atf.require_config_parameter_includes("LaunchParameters", "use_interactive_step")
    # A het job's components are co-scheduled, so a running one needs a node
    # per component.
    atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.mark.parametrize("command", ["salloc", "sbatch", "srun"])
def test_runtime_recorded(command):
    """salloc/sbatch/srun record --runtime on the job, normalized to
    runtime/<name>."""

    # salloc runs its command on this host, outside the job, so only
    # --kill-command ends it once the job is cancelled.
    options = "--runtime=oci"
    if command == "salloc":
        options += " --kill-command"
    job_id = atf.submit_job(
        command, options, "sleep infinity", background=True, fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert (
        atf.get_job_parameter(job_id, "Runtime", fatal=True) == "runtime/oci"
    ), f"{command}: --runtime not recorded as runtime/oci"
    runtime = atf.run_command_output(
        f"squeue -j {job_id} -O Runtime --noheader", fatal=True
    ).strip()
    assert runtime == "runtime/oci", f"{command}: squeue Runtime should be runtime/oci"


def test_runtime_json_key_rename():
    """The job runtime is keyed 'runtime' in v0.0.46 and 'container_type' in
    v0.0.45 (the same struct member was renamed)."""

    job_id = atf.submit_job_sbatch('--runtime=oci --hold --wrap "true"', fatal=True)

    job = json.loads(
        atf.run_command_output(f"scontrol --json show job {job_id}", fatal=True)
    )["jobs"][0]
    assert job.get("runtime") == "runtime/oci", "v0.0.46 'runtime' key missing"
    assert "container_type" not in job, "v0.0.46 should not use 'container_type'"

    job45 = json.loads(
        atf.run_command_output(f"scontrol --json=v0.0.45 show job {job_id}", fatal=True)
    )["jobs"][0]
    assert (
        job45.get("container_type") == "runtime/oci"
    ), "v0.0.45 'container_type' key missing"


@pytest.fixture
def oci_enabled():
    """Enable OCI via a minimal runc oci.conf and remove it on teardown.

    OCI support is global (oci.conf enables --container processing for every
    step), so it is scoped to this fixture to keep it away from the other tests,
    which assume OCI is disabled.
    """

    atf.require_config_file(
        "oci.conf",
        'EnvExclude="^(SLURM_CONF|SLURM_CONF_SERVER)="\n'
        'RunTimeQuery="runc --rootless=true --root=/run/user/%U/ state %n.%u.%j.%s.%t"\n'
        'RunTimeKill="runc --rootless=true --root=/run/user/%U/ kill -a %n.%u.%j.%s.%t"\n'
        'RunTimeDelete="runc --rootless=true --root=/run/user/%U/ delete --force %n.%u.%j.%s.%t"\n'
        'RunTimeRun="runc --rootless=true --root=/run/user/%U/ run %n.%u.%j.%s.%t -b %b"\n',
    )
    yield
    atf.run_command(
        f"rm -f {atf.properties['slurm-config-dir']}/oci.conf",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


@pytest.mark.parametrize("command", ["salloc", "sbatch", "srun"])
@pytest.mark.parametrize(
    "options,parameter,expected,inherited",
    [
        ("--container-id=abc123", "ContainerID", "abc123", "abc123"),
    ],
    ids=["container-id"],
)
def test_hetjob_component_option_propagation(
    command, options, parameter, expected, inherited
):
    """A heterogeneous component that omits an option records what that
    option's propagation rule says, not whatever the previous component
    asked for.

    --container-id is propagated, so the second inherits it. --runtime and
    --container are reset for each component instead, which
    test_hetjob_runtime_mix_recorded covers. heterogeneous_jobs.html lists
    which options fall on which side.

    Only an omitted option can inherit. A component that names the option
    explicitly always wins, so giving every component its own value would
    prove nothing either way.

    The job is held and never launched, so this asserts on the submitted job
    records.
    """

    if command == "sbatch":
        job_id = atf.submit_job_sbatch(
            f"-N1 {options} --hold --wrap=true : -N1", fatal=True
        )
    else:
        # A held salloc or srun never returns, so it has to be backgrounded.
        job_id = atf.submit_job(
            command,
            f"-N1 {options} --hold : -N1",
            "true",
            background=True,
            fatal=True,
        )
    jobs = list(atf.get_jobs(job_id, fatal=True).values())

    assert len(jobs) == 2, "the het job should have two components"
    components = {job["HetJobOffset"]: job for job in jobs}
    assert (
        components[0][parameter] == expected
    ), f"component 0 should record the {parameter} it asked for"
    assert (
        components[1].get(parameter) == inherited
    ), f"component 1 asked for no {parameter} and should record {inherited}"


def test_hetjob_component_runtime_at_step_launch(oci_enabled):
    """Each heterogeneous component launches its step under its own runtime.

    With DefRuntimePlugin=runtime/none, the component naming --runtime=oci
    engages its --container value and fails on an invalid bundle, while the
    component naming no runtime ignores --container and succeeds. Exactly one
    of the two steps failing is what separates per-component plugin selection
    from either component's runtime being used for both.

    The job runs, unlike the submission tests above, because the plugin is
    chosen in slurmstepd rather than at submission.
    """

    atf.set_config_parameter("DefRuntimePlugin", "runtime/none")

    error = atf.run_command_error(
        "srun -N1 --runtime=oci --container=invalid : -N1 --container=invalid true",
        fatal=True,
    )
    assert (
        error.count("Task launch for StepId=") == 1
    ), "only the component that asked for runtime/oci should fail its step launch"


def test_runtime_env_vars():
    """SLURM_RUNTIME is exported to the job."""

    output = atf.run_command_output("srun --runtime=none env", fatal=True)
    assert (
        "SLURM_RUNTIME=runtime/none" in output
    ), "SLURM_RUNTIME should be exported to the job environment"


def test_runtime_env_var_input_and_negative():
    """SRUN_RUNTIME sets --runtime, and SLURM_RUNTIME is only exported when
    --runtime is specified."""

    output = atf.run_command_output(
        "srun env", env_vars="SRUN_RUNTIME=none", fatal=True
    )
    assert (
        "SLURM_RUNTIME=runtime/none" in output
    ), "SRUN_RUNTIME should set the job runtime"

    output = atf.run_command_output("srun env", fatal=True)
    assert (
        "SLURM_RUNTIME=" not in output
    ), "SLURM_RUNTIME should not be exported when --runtime is not specified"


def test_def_runtime_plugin_reported():
    """scontrol show config reports DefRuntimePlugin, defaulting to runtime/oci."""

    atf.set_config_parameter("DefRuntimePlugin", None)
    assert (
        atf.get_config_parameter("DefRuntimePlugin", live=True) == "runtime/oci"
    ), "an unset DefRuntimePlugin should report the documented default runtime/oci"

    atf.set_config_parameter("DefRuntimePlugin", "runtime/none")
    assert (
        atf.get_config_parameter("DefRuntimePlugin", live=True) == "runtime/none"
    ), "DefRuntimePlugin should reflect the configured runtime/none"


def test_def_runtime_plugin_takes_effect(oci_enabled):
    """A job with no --runtime uses the configured DefRuntimePlugin.

    With oci.conf enabled, DefRuntimePlugin=runtime/oci takes the
    SRUN_CONTAINER value and an invalid bundle fails the step launch, while
    DefRuntimePlugin=runtime/none skips SRUN_CONTAINER, which it does not
    provide, and the same job succeeds. The jobs request no --runtime, so the
    contrast proves the configured default selects the plugin.
    """

    atf.set_config_parameter("DefRuntimePlugin", "runtime/oci")
    atf.run_command_error(
        "srun hostname",
        env_vars="SRUN_CONTAINER=invalid",
        xfail=True,
        fatal=True,
    )

    atf.set_config_parameter("DefRuntimePlugin", "runtime/none")
    atf.run_command("srun hostname", env_vars="SRUN_CONTAINER=invalid", fatal=True)


def test_runtime_options_in_help():
    """--help and --usage list the options loaded runtime plugins provide.

    DefRuntimePlugin is loaded before the options are parsed, so with
    runtime/oci as the default, --help and --usage list its --container.
    """

    atf.set_config_parameter("DefRuntimePlugin", "runtime/oci")
    for command in ("salloc", "sbatch", "srun"):
        output = atf.run_command_output(f"{command} --help", fatal=True)
        assert (
            "Options provided by runtime/oci:" in output
        ), f"{command} --help should have a runtime/oci section"
        assert (
            "--container=path" in output
        ), f"{command} --help should list runtime/oci's --container"

        output = atf.run_command_output(f"{command} --usage", fatal=True)
        assert (
            "[--container=value]" in output
        ), f"{command} --usage should list runtime/oci's --container"


def test_runtime_options_in_help_follow_runtime():
    """--help lists a runtime's options only once that runtime is loaded.

    With DefRuntimePlugin=runtime/none nothing provides --container, until an
    earlier --runtime=oci on the command line loads runtime/oci.
    """

    atf.set_config_parameter("DefRuntimePlugin", "runtime/none")
    for command in ("salloc", "sbatch", "srun"):
        output = atf.run_command_output(f"{command} --help", fatal=True)
        assert (
            "--container=path" not in output
        ), f"{command} --help should not list --container under runtime/none"

        output = atf.run_command_output(f"{command} --runtime=oci --help", fatal=True)
        assert (
            "--container=path" in output
        ), f"{command} --runtime=oci --help should list --container"


def _submit_held(command, options, env_vars=None):
    """Submits a held job through command and returns its id.

    The job never launches, so a held salloc or srun, which never returns, is
    backgrounded, and the tests assert on the submitted job record.
    """

    return atf.submit_job(
        command,
        f"-N1 {options} --hold",
        "true",
        background=True,
        fatal=True,
        env_vars=env_vars,
    )


def _rejected(command, options, env_vars=None):
    """Runs command with options, which must fail, and returns its stderr.

    salloc and srun are not held, so one that wrongly accepts the options runs
    true and exits 0 rather than blocking until the command times out.
    """

    line = {
        "sbatch": f"sbatch --hold {options} --wrap=true",
        "salloc": f"salloc {options} true",
        "srun": f"srun {options} true",
    }[command]
    return atf.run_command_error(line, xfail=True, fatal=True, env_vars=env_vars)


# Each case is (options, error, expected job record). An error means the
# submission is refused; otherwise the job record carries what is expected.
runtime_rules = {
    "same-runtime-twice": (
        "--runtime=oci --runtime=oci",
        None,
        {"Runtime": "runtime/oci"},
    ),
    "two-runtimes": (
        "--runtime=oci --runtime=none",
        "--runtime may only be given once",
        None,
    ),
    "none-rejects-container": (
        "--runtime=none --container=/nonexistent/bundle",
        "--container is not an option of runtime/none",
        None,
    ),
    "oci-takes-container": (
        "--runtime=oci --container=/nonexistent/bundle",
        None,
        {"Runtime": "runtime/oci", "Container": "/nonexistent/bundle"},
    ),
    "unknown-runtime": (
        "--runtime=bogus",
        "cannot create runtime context for runtime/bogus",
        None,
    ),
}


@pytest.mark.parametrize("command", ["salloc", "sbatch", "srun"])
@pytest.mark.parametrize(
    "options, error, expected", runtime_rules.values(), ids=runtime_rules.keys()
)
def test_runtime_rules(command, options, error, expected):
    """--runtime may be repeated with the same name but not with another, and
    a runtime only accepts the options it provides.

    salloc, sbatch and srun all load the runtime while they parse their
    options, so each refuses the same submissions before any job exists.
    """

    atf.set_config_parameter("DefRuntimePlugin", "runtime/oci")

    if error:
        assert error in _rejected(command, options), f"{command} {options!r}"
        return

    job_id = _submit_held(command, options)
    for parameter, value in expected.items():
        assert (
            atf.get_job_parameter(job_id, parameter) == value
        ), f"{command} {options!r} should record {parameter}={value}"


@pytest.mark.parametrize("command", ["salloc", "srun"])
def test_runtime_container_space_form(command):
    """--container takes its value after an equal sign, not after a space.

    sbatch is left out: its first pass stops at the path and takes it as the
    batch script. script-container-space-form checks sbatch's #SBATCH lines.
    """

    atf.set_config_parameter("DefRuntimePlugin", "runtime/oci")

    options = "--runtime=oci --container /nonexistent/bundle"
    assert "--container requires a string value" in _rejected(
        command, options
    ), f"{command} {options!r}"


# Each case is (environment suffix and value, options, expected job record).
runtime_env_rules = {
    "env-sets-runtime": (("RUNTIME", "oci"), "", {"Runtime": "runtime/oci"}),
    "command-line-beats-env": (
        ("RUNTIME", "none"),
        "--runtime=oci",
        {"Runtime": "runtime/oci"},
    ),
    "env-container-ignored-by-other-runtime": (
        ("CONTAINER", "/nonexistent/bundle"),
        "--runtime=none",
        {"Runtime": "runtime/none", "Container": None},
    ),
    "env-container-with-oci": (
        ("CONTAINER", "/nonexistent/bundle"),
        "--runtime=oci",
        {"Runtime": "runtime/oci", "Container": "/nonexistent/bundle"},
    ),
}


@pytest.mark.parametrize("command", ["salloc", "sbatch", "srun"])
@pytest.mark.parametrize(
    "env, options, expected", runtime_env_rules.values(), ids=runtime_env_rules.keys()
)
def test_runtime_env_rules(command, env, options, expected):
    """<COMMAND>_RUNTIME picks the runtime, a --runtime on the command line
    beats it, and a <COMMAND>_CONTAINER is ignored, not rejected, when the job
    uses a runtime that does not provide --container."""

    atf.set_config_parameter("DefRuntimePlugin", "runtime/oci")

    name = f"{command.upper()}_{env[0]}"
    job_id = _submit_held(command, options, env_vars=f"{name}={env[1]}")
    for parameter, value in expected.items():
        assert (
            atf.get_job_parameter(job_id, parameter) == value
        ), f"{command} with {name}={env[1]} {options!r} should record {parameter}={value}"


@pytest.mark.parametrize("command", ["salloc", "sbatch", "srun"])
def test_runtime_env_unknown_runtime(command):
    """A <COMMAND>_RUNTIME naming an unknown runtime fails the submission."""

    atf.set_config_parameter("DefRuntimePlugin", "runtime/oci")

    name = f"{command.upper()}_RUNTIME"
    assert "cannot create runtime context for runtime/bogus" in _rejected(
        command, "", env_vars=f"{name}=bogus"
    ), f"{command} with {name}=bogus"


# Each case is (script directives, options, environment, error, expected job
# record).
# #SBATCH lines are processed before the environment and the command line.
batch_script_rules = {
    "script-beats-env": (
        ["--runtime=oci"],
        "",
        "SBATCH_RUNTIME=none",
        None,
        {"Runtime": "runtime/oci"},
    ),
    "script-and-command-line": (
        ["--runtime=oci"],
        "--runtime=none",
        None,
        "--runtime may only be given once",
        None,
    ),
    "script-same-name-twice": (
        ["--runtime=oci"],
        "--runtime=oci",
        None,
        None,
        {"Runtime": "runtime/oci"},
    ),
    "script-none-rejects-container": (
        ["--runtime=none", "--container=/nonexistent/bundle"],
        "",
        None,
        "--container is not an option of runtime/none",
        None,
    ),
    "script-oci-takes-container": (
        ["--runtime=oci", "--container=/nonexistent/bundle"],
        "",
        None,
        None,
        {"Runtime": "runtime/oci", "Container": "/nonexistent/bundle"},
    ),
    "script-container-space-form": (
        ["--runtime=oci", "--container /nonexistent/bundle"],
        "",
        None,
        "--container requires a string value",
        None,
    ),
}


@pytest.mark.parametrize(
    "directives, options, env, error, expected",
    batch_script_rules.values(),
    ids=batch_script_rules.keys(),
)
def test_sbatch_script_runtime_rules(directives, options, env, error, expected):
    """An #SBATCH --runtime beats SBATCH_RUNTIME, and conflicts with a different
    --runtime on the command line."""

    atf.set_config_parameter("DefRuntimePlugin", "runtime/oci")

    script = "runtime_rules.sh"
    atf.make_bash_script(
        script, "".join(f"#SBATCH {d}\n" for d in directives) + "true\n"
    )
    command = f"sbatch --hold {options} {script}"
    if error:
        assert error in atf.run_command_error(
            command, xfail=True, fatal=True, env_vars=env
        ), command
        return

    job_id = atf.submit_job_sbatch(
        f"--hold {options} {script}", fatal=True, env_vars=env
    )
    for parameter, value in expected.items():
        assert (
            atf.get_job_parameter(job_id, parameter) == value
        ), f"{command} should record {parameter}={value}"


# Each component is (options, expected Runtime, expected Container). A
# component with no --runtime records none, whatever the one before it asked.
het_mixes = {
    "issue-50190": [
        ("", None, None),
        ("--runtime=none", "runtime/none", None),
        ("--runtime=oci --container=/nonexistent/a", "runtime/oci", "/nonexistent/a"),
        ("--runtime none", "runtime/none", None),
        ("--container=/nonexistent/b", None, "/nonexistent/b"),
    ],
    "none-then-oci": [
        ("--runtime=none", "runtime/none", None),
        ("--runtime=oci --container=/nonexistent/a", "runtime/oci", "/nonexistent/a"),
    ],
    "oci-then-none": [
        ("--runtime=oci --container=/nonexistent/a", "runtime/oci", "/nonexistent/a"),
        ("--runtime=none", "runtime/none", None),
    ],
    "oci-then-nothing": [
        ("--runtime=oci --container=/nonexistent/a", "runtime/oci", "/nonexistent/a"),
        ("", None, None),
    ],
    "default-container-then-nothing": [
        ("--container=/nonexistent/a", None, "/nonexistent/a"),
        ("", None, None),
    ],
}


@pytest.mark.parametrize("command", ["salloc", "sbatch", "srun"])
@pytest.mark.parametrize("components", het_mixes.values(), ids=het_mixes.keys())
def test_hetjob_runtime_mix_recorded(command, components):
    """Each component of a het job records its own --runtime and --container,
    however the components mix runtime/none, runtime/oci and the default."""

    atf.set_config_parameter("DefRuntimePlugin", "runtime/oci")

    args = " : ".join(f"-N1 --hold {options}" for options, _, _ in components)
    if command == "sbatch":
        job_id = atf.submit_job_sbatch(f"{args} --wrap=true", fatal=True)
    else:
        job_id = atf.submit_job(command, args, "true", background=True, fatal=True)
    jobs = atf.get_jobs(job_id, fatal=True).values()
    records = {job["HetJobOffset"]: job for job in jobs}
    assert len(records) == len(components), "one job record per component"
    for offset, (options, runtime, container) in enumerate(components):
        assert records[offset].get("Runtime") == runtime, (
            f"component {offset} ({options!r}) should record Runtime={runtime},"
            f" got {records[offset].get('Runtime')}"
        )
        assert records[offset].get("Container") == container, (
            f"component {offset} ({options!r}) should record Container={container},"
            f" got {records[offset].get('Container')}"
        )


@pytest.mark.parametrize("command", ["salloc", "sbatch", "srun"])
@pytest.mark.parametrize(
    "later, error",
    [
        ("--runtime=none --container=/nonexistent/bundle", "is not an option of"),
        ("--runtime=oci --runtime=none", "--runtime may only be given once"),
        ("--runtime=bogus", "cannot create runtime context for runtime/bogus"),
    ],
    ids=["none-with-container", "two-runtimes", "unknown-runtime"],
)
def test_hetjob_later_component_runtime_rejected(command, later, error):
    """A component after the first is held to the same rules as the first."""

    atf.set_config_parameter("DefRuntimePlugin", "runtime/oci")

    assert error in _rejected(command, f"-N1 : -N1 {later}"), f"{command} {later!r}"
