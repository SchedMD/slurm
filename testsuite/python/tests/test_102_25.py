############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that re-adding a QOS into a preempt list doesn't corrupt it."""

import pytest

import atf

# Global variables
qos1 = "qos1"
qos2 = "qos2"
qos3 = "qos3"
qos4 = "qos4"
qos5 = "qos5"


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""
    atf.require_version(
        (26, 11),
        "sbin/slurmdbd",
        reason="Ticket 25809: preempt list corruption on re-add fixed in 26.11",
    )
    atf.require_accounting(True)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db():
    # Create test QOS
    atf.run_command(
        f"sacctmgr -i add qos {qos1},{qos2},{qos3},{qos4},{qos5}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i remove qos {qos1},{qos2},{qos3},{qos4},{qos5}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


def preempt_list():
    """Return the set of QOS names in qos1's preempt list."""
    output = atf.run_command_output(
        f"sacctmgr -nP show qos {qos1} format=preempt",
        fatal=True,
    )
    return set(filter(None, output.strip().split(",")))


@pytest.mark.parametrize(
    "initial,add,expected",
    [
        ([qos2, qos3, qos4], qos2, {qos2, qos3, qos4}),
        ([qos2, qos3, qos4], qos3, {qos2, qos3, qos4}),
        ([qos2, qos3, qos4], qos4, {qos2, qos3, qos4}),
        ([qos2, qos3, qos4], f"{qos3},{qos5}", {qos2, qos3, qos4, qos5}),
        ([qos2], qos2, {qos2}),
        ([qos2, qos3, qos4], f"{qos2},{qos3}", {qos2, qos3, qos4}),
        ([qos2, qos3, qos4], f"{qos2},{qos3},{qos4}", {qos2, qos3, qos4}),
    ],
    ids=["first", "middle", "last", "mixed", "single", "adjacent", "all"],
)
def test_preempt_readd(initial, add, expected):
    """Test re-adding a QOS already in a preempt list."""

    atf.run_command(
        f"sacctmgr -i modify qos {qos1} set preempt={','.join(initial)}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Re-adding a QOS already in the list must not drop or fuse the ids
    # around it, so the list only gains the QOSes that were not in it yet
    # (Ticket 25809).
    atf.run_command(
        f"sacctmgr -i modify qos {qos1} set preempt+={add}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        preempt_list() == expected
    ), f"Verify {qos1}'s preempt list is {','.join(sorted(expected))} after adding {add}, got: {sorted(preempt_list())}"

    # Removing what was re-added must leave nothing of it behind, so the
    # re-add did not store a second copy
    atf.run_command(
        f"sacctmgr -i modify qos {qos1} set preempt-={add}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    remaining = expected - set(add.split(","))
    assert (
        preempt_list() == remaining
    ), f"Verify {qos1}'s preempt list is {','.join(sorted(remaining))} after removing {add}, got: {sorted(preempt_list())}"
