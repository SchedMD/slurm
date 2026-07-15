############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sinfo --states name parsing (ticket 25499)."""

import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


# REBOOT_REQUESTED/REBOOT_ISSUED only parse as of 26.05 (ticket 25499); share
# the gate between both params so the version boundary lives in one place.
reboot_name_skip = pytest.mark.skipif(
    atf.get_version("bin/sinfo") < (26, 5),
    reason="Ticket 25499: REBOOT_REQUESTED/REBOOT_ISSUED were not "
    "accepted by --states",
)


@pytest.mark.skipif(
    atf.get_version("bin/sinfo") < (25, 11),
    reason="Ticket 23752: sinfo prints an error and exits non-zero on an "
    "invalid --states value",
)
# npc/perfctrs are the removed PERFCTRS state: rejected on every supported
# version, so this pins the removal against a future re-add.
@pytest.mark.parametrize("state", ["NOTASTATE", "npc", "perfctrs"])
def test_states_invalid_rejected(state):
    """Verify sinfo rejects unknown and removed --states values"""

    error = atf.run_command_error(f"sinfo --states={state}", fatal=True, xfail=True)
    assert (
        "Bad state string" in error
    ), f"sinfo should report a 'Bad state string' error for --states={state}"


def test_states_all_accepted():
    """Verify sinfo --states=all is accepted"""

    exit_code = atf.run_command_exit("sinfo --states=all")
    assert exit_code == 0, "sinfo should accept --states=all"


@pytest.mark.parametrize(
    "state",
    [
        # Compact aliases users filtered with before the fix; these parse on
        # every supported version, so they guard against a refactor dropping
        # the hand-maintained special cases that live outside the state table.
        "reboot",
        "BOOT",
        pytest.param("REBOOT_REQUESTED", marks=reboot_name_skip),
        pytest.param("REBOOT_ISSUED", marks=reboot_name_skip),
    ],
)
def test_states_reboot_names(state):
    """Verify the compact and documented reboot --states names parse"""

    exit_code = atf.run_command_exit(f"sinfo --states={state}")
    assert exit_code == 0, f"sinfo should accept --states={state}"


@pytest.mark.skipif(
    atf.get_version("bin/sinfo") < (26, 11),
    reason="Ticket 25499: --helpstate and --states are table-driven (kept in "
    "sync) as of 26.11",
)
def test_states_helpstate_roundtrip():
    """Verify every node state sinfo --helpstate advertises is accepted by --states.

    --helpstate lists only the canonical names (the compact aliases are omitted
    by design), so this guards the advertised canonical names and the parser
    against drifting apart; it does not cover the compact aliases.
    The list and parser are driven from the same table as of 26.11.
    """

    states = atf.run_command_output("sinfo --helpstate", fatal=True).split()
    assert states, "sinfo --helpstate should list node states"
    for state in states:
        exit_code = atf.run_command_exit(f"sinfo --states={state}")
        assert exit_code == 0, f"sinfo should accept --states={state}"
