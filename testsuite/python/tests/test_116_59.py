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
    atf.require_auto_config("deletes oci.conf directly on teardown")
    atf.require_slurm_running()


@pytest.mark.parametrize("command", ["salloc", "sbatch", "srun"])
def test_runtime_recorded(command):
    """salloc/sbatch/srun record --runtime on the job, normalized to
    runtime/<name>."""

    job_id = atf.submit_job(
        command, "--runtime=oci", "sleep 60", background=True, fatal=True
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


def test_runtime_bogus_fails():
    """An unknown runtime plugin fails when the step is launched.

    Only srun is exercised: it launches a step under slurmstepd, which loads
    the runtime plugin and fails. salloc and sbatch do not validate the
    runtime plugin name at submission time, so they are not tested here.
    """

    error = atf.run_command_error("srun --runtime=bogus true", xfail=True, fatal=True)
    assert (
        "launch failed" in error
    ), "srun should report a step launch failure for an unknown runtime plugin"


def test_runtime_none_ignores_container():
    """runtime/none runs tasks on the host, ignoring the --container value."""

    # runtime/none ignores --container (even an invalid bundle) and runs the
    # task directly on the host, exiting 0.
    atf.run_command("srun --runtime=none --container=invalid hostname", fatal=True)


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


def test_runtime_oci_uses_container(oci_enabled):
    """runtime/oci engages the --container value, unlike runtime/none.

    runtime/oci does not ignore --container, so an invalid bundle fails the
    step launch where runtime/none exits 0 on the same input. A full
    containerized run additionally needs a valid OCI bundle and is out of scope
    here.
    """

    atf.run_command_error(
        "srun --runtime=oci --container=invalid hostname", xfail=True, fatal=True
    )


def test_runtime_env_vars():
    """SLURM_RUNTIME is exported to the job and SBATCH_RUNTIME sets --runtime."""

    output = atf.run_command_output("srun --runtime=none env", fatal=True)
    assert (
        "SLURM_RUNTIME=runtime/none" in output
    ), "SLURM_RUNTIME should be exported to the job environment"

    job_id = atf.submit_job_sbatch(
        '--hold --wrap "true"', env_vars="SBATCH_RUNTIME=oci", fatal=True
    )
    assert (
        atf.get_job_parameter(job_id, "Runtime", fatal=True) == "runtime/oci"
    ), "SBATCH_RUNTIME should set the job runtime"


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

    With oci.conf enabled, DefRuntimePlugin=runtime/oci engages the
    --container value and an invalid bundle fails the step launch, while
    DefRuntimePlugin=runtime/none ignores --container and the same job
    succeeds. The jobs request no --runtime, so the contrast proves the
    configured default selects the plugin.
    """

    atf.set_config_parameter("DefRuntimePlugin", "runtime/oci")
    atf.run_command_error("srun --container=invalid hostname", xfail=True, fatal=True)

    atf.set_config_parameter("DefRuntimePlugin", "runtime/none")
    atf.run_command("srun --container=invalid hostname", fatal=True)
