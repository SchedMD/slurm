############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test reservation KV is not stored without AccountingStoreFlags=resv_kv."""

import time

import pytest

import atf

RESV_NAME = "resv_kv_not_stored"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_config_parameter_excludes("AccountingStoreFlags", "resv_kv")
    atf.require_version(
        (26, 11), "bin/sacctmgr", reason="Issue 51034: KV was added in 26.11"
    )
    atf.require_slurm_running()


@pytest.fixture
def reservation():
    """A started reservation created with KV, deleted after."""

    admin = atf.properties["slurm-user"]
    atf.run_command(
        f"scontrol create reservation reservationname={RESV_NAME} "
        f"starttime=now duration=10 nodecnt=1 users={admin} KV='{{\"a\":1}}'",
        user=admin,
        fatal=True,
    )
    yield RESV_NAME
    atf.run_command(f"scontrol delete reservation={RESV_NAME}", user=admin, fatal=True)


def sacctmgr_periods(name):
    """Return the WithKV output of each accounting period of a reservation."""
    window = "start=now-1hours end=now+1hours"
    output = atf.run_command_output(
        f"sacctmgr show reservation names={name} {window} WithKV", fatal=True
    )
    lines = output.splitlines()
    # Each period is a header naming it, a rule, then the document
    return [lines[i + 2] for i in range(0, len(lines) - 2, 3)]


def test_kv_not_stored(reservation):
    """Verify sacctmgr reports NONE, and a KV change opens no new period."""

    for _ in atf.timer(fatal=True):
        if sacctmgr_periods(reservation):
            break
    assert sacctmgr_periods(reservation) == [
        "NONE"
    ], "sacctmgr should report no KV for the reservation"

    # Accounting periods are keyed on whole seconds, so let the start second
    # pass, or a wrongly opened period would replace the first one unseen
    time.sleep(2)
    admin = atf.properties["slurm-user"]
    atf.run_command(
        f"scontrol update reservationname={reservation} KV='{{\"a\":2}}'",
        user=admin,
        fatal=True,
    )

    # A new period would follow the update in the same message, so give it
    # time to show up. Nothing should, so keep that well under the default
    # timeout.
    for _ in atf.timer(timeout=10, xfail=True):
        periods = sacctmgr_periods(reservation)
        if periods != ["NONE"]:
            break
    assert periods == [
        "NONE"
    ], "A KV change should not open a new accounting period without resv_kv"
