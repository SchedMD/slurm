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
own options or environment, not the job's, so it is tested here too.

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
