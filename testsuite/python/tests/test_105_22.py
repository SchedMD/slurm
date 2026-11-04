############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Validate sbatch --external handling across job and hetjob shapes.

--external comes from the command line or from #SBATCH directives. Each case
checks one of three outcomes: sbatch refuses the submission itself, the
controller refuses it, or the job is created with a particular shape. Created
jobs are inspected while still PENDING and then cancelled, so nothing waits
on a backfill cycle.

BatchFlag is the discriminator throughout: a component that got EXTERNAL_JOB
has no script and reports BatchFlag=0, while one that did not reports
BatchFlag=1.
"""

import re

import pytest

import atf

# One external node per external component of the widest hetjob here
external_nodes = ["extnode1", "extnode2", "extnode3"]

# The one node that runs a slurmd, for components that carry a batch script
compute_node = "node1"

external_partition = "external"
compute_partition = "primary"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # slurmctld is always at least as new as sbatch across supported
    # upgrades, so gating on sbatch covers the controller side too.
    atf.require_version(
        (26, 5, 3),
        "bin/sbatch",
        reason="Issue 50997: per-component --external is available in 26.05.3",
    )

    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Overwrite rather than append: the CR_* allocation types are mutually
    # exclusive, so adding CR_CPU to a config already setting CR_Core_Memory
    # yields "cr_core_memory,CR_CPU", which slurmctld rejects.
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    # This module starts no slurmdbd
    atf.require_accounting(enabled=False)

    # Static EXTERNAL nodes: a node created with "scontrol create" is also
    # flagged DYNAMIC_NORM, which drags the allocation path into alias and
    # address lookups that have nothing to resolve for an external node.
    #
    # The compute node needs NodeAddr because slurmctld has to reach its
    # slurmd and the bare name does not resolve here. The external nodes are
    # deliberately left without one, since nothing should contact a node that
    # has no slurmd.
    node_config = {compute_node: {"CPUs": 4, "NodeAddr": "localhost"}}
    for node_name in external_nodes:
        node_config[node_name] = {"CPUs": 4, "State": "EXTERNAL"}
    atf.require_config_parameter("NodeName", node_config)

    atf.require_config_parameter(
        "PartitionName",
        {
            compute_partition: {"Nodes": compute_node, "Default": "YES"},
            external_partition: {"Nodes": ",".join(external_nodes)},
        },
    )

    # require_slurm_running() starts a slurmd for every NodeName line, and an
    # EXTERNAL node has none, so start the controller and only the compute
    # node's slurmd, as test_141_1 does for static CLOUD nodes.
    atf.start_slurmctld(clean=True)
    atf.start_slurmd(compute_node, wait="idle")

    yield

    # stop_slurmctld() returns failures rather than failing
    failures = atf.stop_slurmctld(also_slurmds=True)
    assert failures is None, f"Failures stopping the daemons: {failures}"


@pytest.fixture(autouse=True)
def submitted_jobs():
    """Collect this test's leader job ids and cancel them afterwards.

    Only leaders: scancel refuses a pending non-leader hetjob component.
    """
    leader_job_ids = []
    yield leader_job_ids
    if leader_job_ids:
        atf.cancel_jobs(leader_job_ids, fatal=True, quiet=True)


@pytest.fixture
def script():
    """A batch script in the test's own working directory."""
    script_name = "job.sh"
    atf.make_bash_script(script_name, "sleep infinity")
    return script_name


@pytest.fixture
def directive_script():
    """Return a function writing a script that defines its own components.

    Each argument gives one component's options. sbatch splits them on the
    "#SBATCH hetjob" directives between them, so the resulting command line
    carries no ":" at all.
    """

    def _directive_script(*component_options):
        script_name = "hetjob.sh"
        directives = "\n#SBATCH hetjob\n".join(
            f"#SBATCH {options}" for options in component_options
        )
        atf.make_bash_script(script_name, f"{directives}\nsleep infinity\n")
        return script_name

    return _directive_script


def submit_refused(args):
    """Run sbatch, failing the test unless it refuses. Returns the result."""
    return atf.run_command(f"sbatch {args}", xfail=True, fatal=True)


def submit_refused_by_controller(args):
    """Run sbatch expecting slurmctld to refuse the job. Returns the result."""
    result = submit_refused(args)
    assert (
        "Submitted batch job" not in result["stdout"]
    ), f"sbatch reported a job: {result['stdout']!r}"
    # The refusal must leave slurmctld running
    assert atf.is_slurmctld_running(), "slurmctld stopped answering after the refusal"
    return result


@pytest.fixture
def submit_ok(submitted_jobs):
    """Return a function running sbatch and returning the leader job id."""

    def _submit_ok(args):
        result = atf.run_command(f"sbatch {args}", fatal=True)
        match = re.search(r"Submitted batch job (\d+)", result["stdout"])
        assert match, f"sbatch printed no job id: {result['stdout']!r}"
        leader_job_id = int(match.group(1))
        submitted_jobs.append(leader_job_id)
        return leader_job_id

    return _submit_ok


def components(leader_job_id):
    """Return the job's components in offset order, leader first."""
    jobs = atf.get_jobs(fatal=True)
    assert leader_job_id in jobs, f"Job {leader_job_id} not found"

    leader = jobs[leader_job_id]
    if not leader.get("HetJobId"):
        return [leader]

    return [jobs[job_id] for job_id in sorted(atf.range_to_list(leader["HetJobIdSet"]))]


def batch_flags(leader_job_id):
    """Return each component's BatchFlag, in offset order."""
    return [component["BatchFlag"] for component in components(leader_job_id)]


# ---------------------------------------------------------------------------
# Single component: unchanged behaviour, no ":" anywhere
# ---------------------------------------------------------------------------


def test_single_regular_job(submit_ok, script):
    """A plain batch job still carries its script."""

    leader_job_id = submit_ok(f"-n1 -p {compute_partition} {script}")

    assert batch_flags(leader_job_id) == [1], "Regular job should be a batch job"


def test_single_external_job(submit_ok):
    """--external alone submits a scriptless job."""

    leader_job_id = submit_ok(f"--external -n1 -p {external_partition}")

    assert batch_flags(leader_job_id) == [0], "External job should have no script"


def test_single_external_with_script_refused(script):
    """--external and a script contradict each other."""

    result = submit_refused(f"--external -n1 {script}")

    assert (
        "script arguments not permitted" in result["stderr"].lower()
    ), f"Unexpected refusal: {result['stderr']!r}"


def test_single_external_with_wrap_refused():
    """--external and --wrap contradict each other."""

    result = submit_refused('--external -n1 --wrap="sleep infinity"')

    assert (
        "wrap option not permitted" in result["stderr"].lower()
    ), f"Unexpected refusal: {result['stderr']!r}"


# ---------------------------------------------------------------------------
# Hetjobs
# ---------------------------------------------------------------------------


def test_het_all_external(submit_ok):
    """Every component external: no component carries a script."""

    leader_job_id = submit_ok(
        f"--external -n1 -p {external_partition}"
        f" : --external -n1 -p {external_partition}"
    )

    assert batch_flags(leader_job_id) == [0, 0], "No component should be a batch job"


def test_het_mixed_leader_wrap(submit_ok):
    """--wrap stands in for the leader's script in a mixed hetjob."""

    leader_job_id = submit_ok(
        f'--wrap="sleep infinity" -n1 -p {compute_partition}'
        f" : -n1 --external -p {external_partition}"
    )

    assert batch_flags(leader_job_id) == [
        1,
        0,
    ], "Leader should carry the wrapped script and the second component should not"


def test_het_mixed_three_components(submit_ok, script):
    """A regular leader, an external component and a further regular one."""

    leader_job_id = submit_ok(
        f"-n1 -p {compute_partition}"
        f" : -n1 --external -p {external_partition}"
        f" : -n1 -p {compute_partition} {script}"
    )

    assert batch_flags(leader_job_id) == [
        1,
        0,
        1,
    ], "Only the external component should lack a script"


def test_het_external_leader_with_script_refused(script):
    """An external leader cannot be given a script, even in a hetjob.

    sbatch refuses this itself, since the script belongs to the leader.
    """

    result = submit_refused(f"--external -n1 : -n1 -p {compute_partition} {script}")

    assert (
        "script arguments not permitted" in result["stderr"].lower()
    ), f"Unexpected refusal: {result['stderr']!r}"


def test_het_external_leader_with_regular_component_refused():
    """An external leader cannot be followed by a regular component.

    Nothing on the command line is contradictory, so sbatch builds it and the
    controller is the one that refuses: only the leader supplies the shared
    script and an external leader has none.
    """

    result = submit_refused_by_controller(
        f"--external -n1 -p {external_partition}" f" : -n1 -p {compute_partition}"
    )

    # Match what only the controller says. sbatch's own refusals in this file
    # also carry the word "external", so a looser match would still pass if a
    # change made sbatch reject the command line before ever submitting it.
    stderr = result["stderr"].lower()
    assert (
        "cannot follow an external hetjob leader" in stderr
    ), f"Unexpected refusal: {result['stderr']!r}"
    assert (
        "invalid configuration for external job" in stderr
    ), f"Unexpected refusal: {result['stderr']!r}"


# ---------------------------------------------------------------------------
# Components defined by the script rather than the command line
# ---------------------------------------------------------------------------


def test_script_defined_mixed_hetjob(submit_ok, directive_script):
    """A script names its own components, one of them external.

    There is no ":" on the command line here, so nothing about the components
    is known until the script is read, one component at a time.
    """

    script_name = directive_script(
        f"-n1 -p {compute_partition}",
        f"-n1 --external -p {external_partition}",
    )

    leader_job_id = submit_ok(script_name)

    assert batch_flags(leader_job_id) == [
        1,
        0,
    ], "Leader should carry the script and the second component should not"


def test_script_defined_external_does_not_carry_forward(submit_ok, directive_script):
    """A directive marks the component it appears under and no later one."""

    script_name = directive_script(
        f"-n1 -p {compute_partition}",
        f"-n1 --external -p {external_partition}",
        f"-n1 -p {compute_partition}",
    )

    leader_job_id = submit_ok(script_name)

    assert batch_flags(leader_job_id) == [
        1,
        0,
        1,
    ], "Only the component given --external should lack a script"


# ---------------------------------------------------------------------------
# --external given by a directive on the first component
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "component_options, expected_messages",
    [
        pytest.param(
            [f"-n1 --external -p {external_partition}", f"-n1 -p {compute_partition}"],
            [
                "cannot follow an external hetjob leader",
                "invalid configuration for external job",
            ],
            id="external_leader_regular_component",
        ),
        pytest.param(
            [
                f"-n1 --external -p {external_partition}",
                f"-n1 --external -p {external_partition}",
            ],
            ["invalid configuration for external job"],
            id="all_external",
        ),
        pytest.param(
            [f"--external -n1 -p {external_partition}"],
            ["invalid configuration for external job"],
            id="single_job",
        ),
    ],
)
def test_script_defined_external_first_component_refused(
    directive_script, component_options, expected_messages
):
    """A directive cannot make the first component external.

    sbatch reads the script before it parses the directives, so the first
    component keeps the script and the controller refuses the job.
    """

    result = submit_refused_by_controller(directive_script(*component_options))

    stderr = result["stderr"].lower()
    for message in expected_messages:
        assert message in stderr, f"Unexpected refusal: {result['stderr']!r}"


# ---------------------------------------------------------------------------
# Either side of the leader/component boundary
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "second_component",
    [
        pytest.param(f"--external -n1 -p {external_partition}", id="first_token"),
        pytest.param(f"-n1 --external -p {external_partition}", id="after_n1"),
    ],
)
def test_external_in_second_component(submit_ok, script, second_component):
    """--external in the second component marks it alone, wherever it sits.

    As the first token it sits right after the ":", next to the leader's
    options. Either way the leader keeps the script.
    """

    leader_job_id = submit_ok(
        f"-n1 -p {compute_partition} : {second_component} {script}"
    )

    assert batch_flags(leader_job_id) == [
        1,
        0,
    ], "Leader should carry the script and the second component should not"


def test_external_last_token_of_leader():
    """--external closing the leader belongs to it."""

    result = submit_refused(
        f"-n1 -p {compute_partition} --external"
        f" : -n1 -p {external_partition}"
        ' --wrap="sleep infinity"'
    )

    assert (
        "wrap option not permitted" in result["stderr"].lower()
    ), f"Unexpected refusal: {result['stderr']!r}"
