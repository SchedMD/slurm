############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test --constraint with more than 64 matching-OR alternatives.

slurmctld records which matching-OR alternatives ("[f0|f1|...]") each node
configuration satisfies in a bitmap. Before ticket 25977 was fixed, that bitmap
was a fixed 64 bits, so a job with more alternatives wrote past it and
corrupted the slurmctld heap. The bitmap now has at least one bit per
alternative, with DEFAULT_FEATURES (64) in src/slurmctld/node_scheduler.c as
its smallest size.

One node has alternatives f0 through f83, and the other nodes have only f84.
Jobs with 64 and with 85 alternatives must be scheduled, with slurmctld still
running afterwards. The 85th alternative must be honored on its own, and must
be the one chosen when two nodes have to share an alternative.
"""

import pytest

import atf

# DEFAULT_FEATURES in src/slurmctld/node_scheduler.c, the old fixed size.
DEFAULT_BITMAP_SIZE = 64
# Ticket 25977 reproducer: 85 alternatives (f0 through f84).
OVERFLOW_COUNT = 85
LAST_FEATURE = f"f{OVERFLOW_COUNT - 1}"
# validate_job_create_req() rejects a --constraint longer than this.
MAX_CONSTRAINT_LEN = 1024


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        component="sbin/slurmctld",
        reason="Ticket 25977: matching-OR constraints with more than 64 "
        "alternatives corrupt the slurmctld heap before this fix",
    )
    # Every node starts with only the last alternative. The first node then
    # gets all the others instead, so it does not share an alternative with
    # the rest.
    nodes = atf.require_nodes(3, [("CPUs", 1), ("Features", LAST_FEATURE)])
    atf.set_node_parameter(
        nodes[0], "Features", ",".join(f"f{i}" for i in range(OVERFLOW_COUNT - 1))
    )
    atf.require_slurm_running()
    return {"low": nodes[0], "last": nodes[1:]}


@pytest.fixture
def restore_controller(setup):
    """Bring slurmctld back if the case under test aborted it.

    Later tests, and conftest job cleanup, need a controller. Start clean so
    the job that aborted it is not reloaded from saved state.
    """

    yield
    if atf.is_slurmctld_running(quiet=True) or not atf.properties["auto-config"]:
        return
    atf.start_slurmctld(clean=True, quiet=True)


def _constraint(count):
    """Build a matching-OR constraint of count alternatives, f0|f1|..."""
    constraint = "[" + "|".join(f"f{i}" for i in range(count)) + "]"
    assert len(constraint) <= MAX_CONSTRAINT_LEN, (
        f"constraint is {len(constraint)} bytes; submit rejects it above "
        f"{MAX_CONSTRAINT_LEN}, so it would never reach scheduling"
    )
    return constraint


def _schedule(count, sbatch_args="-N1"):
    """Submit a job with count matching-OR alternatives and wait for it.

    Fails if slurmctld stops answering. That is how the heap corruption shows
    up: the abort can land during sbatch or on a later allocation.
    """
    job_id = atf.submit_job_sbatch(
        f"{sbatch_args} --time=1 --constraint='{_constraint(count)}' --wrap true"
    )
    if not atf.is_slurmctld_running(quiet=True):
        pytest.fail(
            f"slurmctld died scheduling a matching-OR constraint with {count} "
            "alternatives"
        )
    assert job_id, "sbatch failed to submit the job"

    for _ in atf.timer(fatal=True):
        if not atf.is_slurmctld_running(quiet=True):
            pytest.fail(
                f"slurmctld died after submitting job {job_id} with {count} "
                "matching-OR alternatives"
            )
        state = atf.get_job_parameter(job_id, "JobState", quiet=True)
        if atf.is_job_state_done(state):
            break
    assert state == "COMPLETED", f"job {job_id} ended in {state}, expected COMPLETED"
    return job_id


def test_matching_or_at_default_size():
    """A matching-OR constraint of exactly 64 alternatives is scheduled."""
    _schedule(DEFAULT_BITMAP_SIZE)


def test_matching_or_past_default_size(restore_controller):
    """A matching-OR constraint of more than 64 alternatives is scheduled.

    slurmctld must stay up. Before the fix, when the feature bitmap was a fixed
    64 bits, this aborted the controller instead.
    """
    _schedule(OVERFLOW_COUNT)


def test_matching_or_last_alternative(setup, restore_controller):
    """The 85th alternative alone can satisfy the job.

    The job may only use a node whose sole feature is the last alternative,
    so every earlier alternative fails and the 85th must be honored.
    """
    _schedule(OVERFLOW_COUNT, f"-N1 -w {setup['last'][0]}")


def test_matching_or_shared_last_alternative(setup, restore_controller):
    """Two nodes share an alternative past the 64th.

    The last alternative is the only one two nodes have in common, so a
    two-node job must run on nodes that both have it.
    """
    job_id = _schedule(OVERFLOW_COUNT, "-N2")
    nodes = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert len(nodes) == 2, f"job {job_id} ran on {nodes}, expected 2 nodes"
    assert set(nodes) <= set(
        setup["last"]
    ), f"job {job_id} ran on {nodes}, expected nodes with {LAST_FEATURE}"
