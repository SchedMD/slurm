############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that a step, and each component of a het step, runs in its own bundle.

A het step can mix runtimes per component: some under runtime/none, some under
runtime/oci with a --container bundle, some taking DefRuntimePlugin. Each
component given a bundle must run its tasks in that bundle, with
SLURM_CONTAINER naming it, and every other component must run its tasks on the
host without SLURM_CONTAINER. runtime/none and runtime/oci with no bundle both
run tasks on the host, so this does not tell those two apart.

salloc's interactive step is a separate srun that takes its container from its
own options or environment, not the job's, so it is tested here too, as is what
salloc exports to a command it runs.

No real container runtime is needed. With IgnoreFileConfigJson, runtime/oci
runs RunTimeRun as the job user without reading a config.json, so the one
configured here prints a marker file from the bundle's rootfs and the
SLURM_CONTAINER line of the task's environment file, then execs the task on
the host. A task's output therefore shows whether runtime/oci ran it, and
with which bundle.
"""

import collections
import os
import re

import pytest

import atf

task_marker = "task_ran"

# The het step with the most nodes below: 1+1+1+1+2.
max_nodes = 6


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    # --runtime is parsed by the client, and client commands are upgraded
    # after every daemon, so gating srun also covers slurmd and slurmctld.
    atf.require_version(
        (26, 11), "bin/srun", reason="Issue 50190: --runtime was added in 26.11"
    )
    # Components that name no --runtime must land on runtime/oci.
    atf.require_config_parameter("DefRuntimePlugin", "runtime/oci")
    # salloc only takes --container for an interactive step, and one that
    # needs no terminal is enough to show where its task ran.
    atf.require_config_parameter_includes("LaunchParameters", "use_interactive_step")
    atf.require_config_parameter(
        "InteractiveStepOptions", f'"--interactive --preserve-env echo {task_marker}"'
    )
    atf.require_config_file(
        "oci.conf",
        "IgnoreFileConfigJson=true\n"
        "CreateEnvFile=newline\n"
        'RunTimeRun="cat %b/rootfs/marker; grep ^SLURM_CONTAINER= %e; exec %@"\n',
    )
    # Only backfill starts het jobs, so run it every second.
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    # A het job's components are co-scheduled, so each needs its own nodes.
    atf.require_nodes(max_nodes)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def bundles():
    """Two bundles in the test's cwd, each a rootfs holding only a marker
    naming the bundle. --container takes an absolute path."""

    paths = {}
    for name in ("a", "b"):
        bundle = os.path.abspath(f"bundle_{name}")
        atf.run_command(
            f"mkdir -p {bundle}/rootfs && echo in_rootfs_{name} > {bundle}/rootfs/marker",
            fatal=True,
        )
        paths[name] = bundle
    return paths


# Each component is (options, bundle name or None, node count). A component
# with no bundle runs on the host whatever its runtime: runtime/none because it
# ignores containers, and runtime/oci because it has no bundle to run in. The
# cases with one component show that a step that is not heterogeneous runs in
# its bundle too.
permutations = {
    "oci": [("--runtime=oci --container={a}", "a", 1)],
    "default_container": [("--container={a}", "a", 1)],
    "none,oci": [
        ("--runtime=none", None, 1),
        ("--runtime=oci --container={a}", "a", 1),
    ],
    "oci,none": [
        ("--runtime=oci --container={a}", "a", 1),
        ("--runtime=none", None, 1),
    ],
    "oci_a,oci_b": [
        ("--runtime=oci --container={a}", "a", 1),
        ("--runtime=oci --container={b}", "b", 1),
    ],
    # The mix from Issue 50190: DefRuntimePlugin with and without a bundle,
    # interleaved with runtime/none, and a multi-node component.
    "default,none,oci,none,default_container": [
        ("", None, 1),
        ("--runtime=none", None, 1),
        ("--runtime=oci --container={a}", "a", 1),
        ("--runtime none", None, 1),
        ("--container={b}", "b", 2),
    ],
}


@pytest.mark.parametrize("components", permutations.values(), ids=permutations.keys())
def test_step_runtime_per_component(bundles, components):
    """Every task shows exactly its own component's bundle, or none."""

    args = " : ".join(
        f"-N{nodes} {options.format(**bundles)}" for options, _, nodes in components
    )
    result = atf.run_command(
        f"srun -l {args} /bin/sh -c 'echo {task_marker}; printenv SLURM_CONTAINER; true'"
    )
    assert result["exit_code"] == 0, f"srun failed: {result['stderr']}"

    lines = collections.defaultdict(list)
    for line in result["stdout"].splitlines():
        match = re.match(r"^\s*(\d+): (.*)$", line)
        if match:
            lines[int(match.group(1))].append(match.group(2))

    # srun -l numbers tasks across components in component order, one task
    # per node here.
    task = 0
    for comp, (options, bundle, nodes) in enumerate(components):
        if bundle:
            expected = [
                f"in_rootfs_{bundle}",
                f"SLURM_CONTAINER={bundles[bundle]}",
                task_marker,
            ]
        else:
            expected = [task_marker]
        for _ in range(nodes):
            assert sorted(lines[task]) == sorted(expected), (
                f"task {task} of component {comp} ({options!r}) should show"
                f" {expected}, got {lines[task]}"
                f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
            )
            task += 1

    assert sorted(lines) == list(
        range(task)
    ), f"expected output from tasks 0-{task - 1} only, got tasks {sorted(lines)}"


def test_salloc_interactive_step_runs_in_bundle(bundles):
    """salloc --container runs its interactive step in the bundle.

    The interactive step's srun takes its container from its own options or
    environment rather than the job's, so salloc has to hand the bundle to that
    srun for the step to run in it.
    """

    result = atf.run_command(f"salloc -N1 --container={bundles['a']}")
    assert result["exit_code"] == 0, f"salloc failed: {result['stderr']}"

    lines = result["stdout"].splitlines()
    for expected in (
        "in_rootfs_a",
        f"SLURM_CONTAINER={bundles['a']}",
        task_marker,
    ):
        assert expected in lines, (
            f"the interactive step should print {expected!r}"
            f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
        )


@pytest.fixture
def no_interactive_step():
    """Drops use_interactive_step for one test. The other tests need it, so it
    is put back afterwards."""

    atf.remove_config_parameter_value("LaunchParameters", "use_interactive_step")
    yield
    atf.add_config_parameter_value("LaunchParameters", "use_interactive_step")


def test_salloc_container_requires_interactive_step(bundles, no_interactive_step):
    """salloc refuses --container without use_interactive_step.

    salloc has no step of its own to run in a bundle, so it needs an
    interactive step to hand --container to.
    """

    result = atf.run_command(f"salloc -N1 --container={bundles['a']} true", xfail=True)

    assert result["exit_code"] != 0, "salloc should refuse --container"
    assert (
        "--container requires LaunchParameters=use_interactive_step" in result["stderr"]
    ), f"salloc should say why it refused --container: {result['stderr']!r}"


def test_salloc_command_gets_container(bundles):
    """salloc --container gives the command it runs SLURM_CONTAINER.

    salloc(1) lists SLURM_CONTAINER among the variables it sets for the
    executed program when --container is given. The command runs under salloc
    itself, outside any step, so it sees salloc's own environment.
    """

    result = atf.run_command(
        f"salloc -N1 --container={bundles['a']} printenv SLURM_CONTAINER"
    )
    assert result["exit_code"] == 0, f"salloc failed: {result['stderr']}"
    assert result["stdout"].splitlines() == [bundles["a"]], (
        f"the command should see SLURM_CONTAINER={bundles['a']}"
        f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
    )


def test_salloc_command_gets_no_runtime_inputs(bundles):
    """salloc --container exports no runtime input variables to a command.

    Only the interactive step's srun is given the bundle as a runtime input.
    A command salloc runs must not get SRUN_CONTAINER, SBATCH_CONTAINER or
    SALLOC_CONTAINER, or every srun, sbatch or salloc it starts would use the
    bundle.
    """

    result = atf.run_command(
        f"salloc -N1 --container={bundles['a']} /bin/sh -c "
        "'printenv SRUN_CONTAINER SBATCH_CONTAINER SALLOC_CONTAINER; true'"
    )
    assert result["exit_code"] == 0, f"salloc failed: {result['stderr']}"
    assert result["stdout"] == "", (
        "the command should get none of SRUN_CONTAINER, SBATCH_CONTAINER or"
        " SALLOC_CONTAINER"
        f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
    )


def _markers(output):
    """The bundle markers runtime/oci's RunTimeRun printed, one per run."""

    return [line for line in output.splitlines() if line.startswith("in_rootfs_")]


# Each case is (batch components, bundle of the batch step or None, id). The
# batch step runs once, in the first component, so only that component's
# runtime and bundle apply to it. The batch script then runs a step of its own,
# which takes no bundle from the job.
batch_cases = {
    "oci": (["--runtime=oci --container={a}"], "a"),
    "default": (["--container={a}"], "a"),
    "none": (["--runtime=none"], None),
    "oci,oci": (
        ["--runtime=oci --container={a}", "--runtime=oci --container={b}"],
        "a",
    ),
    "oci,none": (["--runtime=oci --container={a}", "--runtime=none"], "a"),
    "none,oci": (["--runtime=none", "--runtime=oci --container={b}"], None),
    "default,oci": (["--container={a}", "--runtime=oci --container={b}"], "a"),
    "none,oci,oci": (
        [
            "--runtime=none",
            "--runtime=oci --container={a}",
            "--runtime=oci --container={b}",
        ],
        None,
    ),
}


@pytest.mark.parametrize(
    "components, bundle", batch_cases.values(), ids=batch_cases.keys()
)
def test_sbatch_batch_step_uses_first_component(bundles, components, bundle):
    """The batch step of an sbatch job runs in its first component's bundle.

    A heterogeneous batch job runs one batch script, in its first component, so
    the runtime of any other component does not apply to it. A step the script
    starts takes no bundle from the job, so it runs on the host.
    """

    script = "batch_runtime.sh"
    atf.make_bash_script(
        script, f"echo {task_marker}\nsrun -N1 /bin/sh -c 'echo step_ran'\n"
    )
    output = "batch_runtime.out"
    args = " : ".join(f"-N1 {c.format(**bundles)}" for c in components)
    job_id = atf.submit_job_sbatch(f"{args} --output={output} {script}", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    lines = atf.run_command_output(f"cat {output}", fatal=True).splitlines()
    for expected in (task_marker, "step_ran"):
        assert expected in lines, f"the batch job should print {expected!r}: {lines}"
    expected_markers = [f"in_rootfs_{bundle}"] if bundle else []
    assert _markers("\n".join(lines)) == expected_markers, (
        f"only the batch step should run in the first component's bundle,"
        f" expected {expected_markers}, got {lines}"
    )
    if bundle:
        assert (
            f"SLURM_CONTAINER={bundles[bundle]}" in lines
        ), f"the batch step should see SLURM_CONTAINER={bundles[bundle]}: {lines}"


# Each case is (components, expected bundle of the interactive step or None).
# salloc starts one srun for its interactive step, and that srun runs in the
# first component, so salloc hands it only that component's bundle. The
# bundles of the others are ignored, whatever runtime they name. A component
# that names no --runtime takes DefRuntimePlugin, runtime/oci here.
salloc_mixes = {
    "default_a,default_b": (["--container={a}", "--container={b}"], "a"),
    "nothing,default_b": (["", "--container={b}"], None),
    "oci,none": (["--runtime=oci --container={a}", "--runtime=none"], "a"),
    "none,oci": (["--runtime=none", "--runtime=oci --container={b}"], None),
    "oci,oci": (
        ["--runtime=oci --container={a}", "--runtime=oci --container={b}"],
        "a",
    ),
    "none,oci,oci": (
        [
            "--runtime=none",
            "--runtime=oci --container={a}",
            "--runtime=oci --container={b}",
        ],
        None,
    ),
}


@pytest.mark.parametrize(
    "components, bundle", salloc_mixes.values(), ids=salloc_mixes.keys()
)
def test_salloc_het_interactive_step_uses_first_component(bundles, components, bundle):
    """The interactive step of a het salloc runs in its first component's
    bundle, with SLURM_CONTAINER naming it, or on the host when that
    component has none. The bundles of the other components are ignored."""

    args = " : ".join(f"-N1 {c.format(**bundles)}" for c in components)
    result = atf.run_command(f"salloc {args}")
    assert result["exit_code"] == 0, f"salloc failed: {result['stderr']}"

    lines = result["stdout"].splitlines()
    assert task_marker in lines, (
        "the interactive step should have run"
        f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
    )
    # Only RunTimeRun prints SLURM_CONTAINER, so it cannot be checked on the host.
    if bundle:
        containers = [line for line in lines if line.startswith("SLURM_CONTAINER=")]
        assert containers == [f"SLURM_CONTAINER={bundles[bundle]}"], (
            f"the interactive step should see SLURM_CONTAINER={bundles[bundle]}"
            f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
        )
    expected = [f"in_rootfs_{bundle}"] if bundle else []
    assert _markers(result["stdout"]) == expected, (
        f"the interactive step should use the first component's bundle,"
        f" expected {expected}"
        f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
    )
