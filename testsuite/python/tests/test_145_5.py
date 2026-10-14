############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify --prefer falls back to --constraint under ignore_prefer_validation.

With SchedulerParameters=ignore_prefer_validation the controller accepts a
--prefer list it cannot satisfy, and the scheduler then evaluates the job using
only --constraint. Every --prefer job below is therefore expected to run.

Ticket 25788: when some, but fewer than requested, nodes had the --prefer
features, the job was held with Reason=BadConstraints and Priority=0 instead of
being retested against --constraint.
"""

import pytest

import atf

# Gate for the fallback when --prefer matches some nodes but fewer than the
# job needs.
PARTIAL_MATCH_VERSION = (26, 5, 5)
PARTIAL_MATCH_REASON = (
    "Ticket 25788: a --prefer list matching some but not enough nodes held "
    "the job instead of falling back to --constraint"
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_includes(
        "SchedulerParameters", "ignore_prefer_validation"
    )
    # Batch jobs never start during the submit RPC, so every job below waits
    # for a scheduling pass. Keep that wait short.
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    # The fallback under test is the main scheduler's. Backfill could start a
    # job before the main scheduler evaluates it and hide a regression there.
    atf.require_config_parameter("SchedulerType", "sched/builtin")
    # Exactly one node carries f1, so a two node --prefer=f1 request matches
    # some, but not enough, nodes.
    atf.require_nodes(1, [("CPUs", 1), ("Features", "f1")])
    atf.require_nodes(2, [("CPUs", 1), ("Features", "f2")])
    atf.require_slurm_running()


def run_job(job_params):
    """Submit a batch job, require that it runs, and return its job id.

    Reports the held-job signature explicitly, since a bare wait_for_job_state()
    timeout does not say whether the job was held or merely slow.
    """

    job_id = atf.submit_job("sbatch", job_params, "hostname", fatal=True)

    if not atf.wait_for_job_state(job_id, "DONE"):
        job = atf.get_job(job_id)
        pytest.fail(
            f"Job {job_id} never ran: JobState={job.get('JobState')} "
            f"Reason={job.get('Reason')} Priority={job.get('Priority')}. "
            "Reason=BadConstraints with Priority=0 means the --prefer pass "
            "held the job instead of falling back to --constraint."
        )

    return job_id


def first_node_features(job_id):
    """Return the list of features of the first node a job ran on."""

    nodes = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert nodes, f"Verify job {job_id} recorded a node list"
    return atf.get_node_parameter(nodes[0], "features", default=[])


def test_unknown_prefer_feature_runs():
    """Verify a job whose --prefer feature exists on no node still runs"""

    job_id = run_job("-N1 --prefer=invalid -o /dev/null")

    assert (
        atf.get_job_parameter(job_id, "NumNodes") == 1
    ), "Verify the job ran on one node"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < PARTIAL_MATCH_VERSION,
    reason=PARTIAL_MATCH_REASON,
)
def test_too_few_prefer_nodes_runs():
    """Verify a job runs when fewer nodes than requested have the --prefer feature"""

    # Two nodes are requested but only one carries f1, so the preferred pass
    # finds a non-empty node set that is still too small.
    job_id = run_job("-N2 --prefer=f1 -o /dev/null")

    assert (
        atf.get_job_parameter(job_id, "NumNodes") == 2
    ), "Verify the job ran on two nodes"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < PARTIAL_MATCH_VERSION,
    reason=PARTIAL_MATCH_REASON,
)
def test_unsatisfiable_prefer_count_runs():
    """Verify a job runs when a counted --prefer expression cannot be satisfied"""

    # The same shortfall expressed as a count: two f1 nodes are asked for and
    # only one exists.
    job_id = run_job('-n3 --ntasks-per-node=1 --prefer "[f1*2&f2*1]" -o /dev/null')

    assert (
        atf.get_job_parameter(job_id, "NumNodes") == 3
    ), "Verify the job ran on three nodes"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < PARTIAL_MATCH_VERSION,
    reason=PARTIAL_MATCH_REASON,
)
def test_too_few_prefer_nodes_falls_back_to_constraint():
    """Verify the retest after a too-small --prefer node set honors --constraint"""

    # Only one node has f1, so the preferred pass fails and the retest must
    # place the job on the two f2 nodes.
    job_id = run_job("-N2 --prefer=f1 --constraint=f2 -o /dev/null")

    nodes = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert len(nodes) == 2, f"Verify the job ran on two nodes, got {nodes}"
    for node in nodes:
        assert "f2" in atf.get_node_parameter(
            node, "features", default=[]
        ), f"Verify {node} has the constrained feature f2"


def test_satisfiable_prefer_still_steers_placement():
    """Verify a satisfiable --prefer still selects the preferred node"""

    # Guards against a fallback that makes --prefer a no-op rather than a
    # preference. The f1 node lacks f2, so a job that ignored --prefer would
    # land on an f2 node instead.
    job_id = run_job("-N1 --constraint=f2 --prefer=f1 -o /dev/null")

    assert "f1" in first_node_features(job_id), "Verify the job ran on the f1 node"


def test_unknown_prefer_falls_back_to_constraint():
    """Verify the fallback tests --constraint alone once --prefer has failed

    This is the second half of the switch between the two lists.
    test_145_4::test_prefer_evaluated_instead_of_constraint covers the first:
    while --prefer can be satisfied, --constraint is not applied at all. Here
    --prefer cannot be satisfied, so the retest uses --constraint alone.
    """

    # The f1 node is where a job with no feature requirement lands, so ask for
    # f2: a retest that dropped --constraint would land on the f1 node.
    job_id = run_job("-N1 --constraint=f2 --prefer=invalid -o /dev/null")

    assert "f2" in first_node_features(job_id), "Verify --constraint was honored"


def test_constraint_still_rejected():
    """Verify ignore_prefer_validation does not soften --constraint"""

    # Only one node has f1, so a two node hard constraint can never be
    # satisfied and must still be refused at submit.
    result = atf.run_command(
        "sbatch -N2 --constraint=f1 -o /dev/null --wrap 'hostname'", xfail=True
    )

    assert result["exit_code"] != 0, "Verify that job was NOT submitted"
    assert (
        "Requested node configuration is not available" in result["stderr"]
    ), "Verify that the job was rejected for an unsatisfiable --constraint"
