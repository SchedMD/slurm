############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_130_7.py - TresDecayHalfLife with PriorityDecayHalfLife turned off.

Ticket 50920: a per-TRES half-life overrides PriorityDecayHalfLife for that
TRES, and it has to keep doing so when PriorityDecayHalfLife is 0.

That case needs its own module because it needs its own PriorityDecayHalfLife,
and it is the one the decay thread has a shortcut for: a global half-life of 0
makes the global factor exactly 1, which leaves every counter where it was, so
the pass is skipped. It may only be skipped when nothing asked for a rate of
its own, which is what is asserted here.

Two accounts share one window. The account with an override has to lose usage
even though the global decay is off, and the account without one has to hold,
because 1 is what the global factor is. A shortcut that skipped the pass
outright would leave both holding and only the first assertion would notice.
"""

import pytest
import tres_decay as td

import atf

# 340s+: tox.ini defines the marker for anything over 60s.
pytestmark = pytest.mark.slow

# Short enough that several ticks land inside the window below.
OVERRIDE_HALF_LIFE_SEC = 60

# PriorityDecayHalfLife=0 is only a legal configuration together with a
# PriorityUsageResetPeriod, so pick the one furthest out: a reset zeroes
# usage_tres_raw regardless of any half-life, which would look exactly like
# the decay this module is trying to measure.
RESET_PERIOD = "Quarterly"

# Usage accrues as cpus * seconds / 60, so a wide job reaches the same
# TRES-minutes in a fraction of the wall time; the nodes are Slurm's own
# configuration rather than the host's, so the width costs nothing.
JOB_RUN_SEC = 40
DECAY_WAIT_SEC = 300
JOB_CPUS = 32

VERSION_REASON = "Ticket 50920: TresDecayHalfLife requires Slurm 26.11"

# The override has to decay, the plain account has to hold.
ACCTS = {
    "override": f"cpu={OVERRIDE_HALF_LIFE_SEC}",
    "global": None,
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((26, 11), "bin/sacctmgr", reason=VERSION_REASON)
    atf.require_version((26, 11), "sbin/slurmdbd", reason=VERSION_REASON)
    atf.require_version((26, 11), "sbin/slurmctld", reason=VERSION_REASON)
    atf.require_auto_config("sets PriorityDecayHalfLife to 0")
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    atf.require_config_parameter("PriorityCalcPeriod", 1)
    atf.require_config_parameter("PriorityDecayHalfLife", 0)
    atf.require_config_parameter("PriorityUsageResetPeriod", RESET_PERIOD)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_accounting(modify=True)
    atf.require_nodes(len(ACCTS), [("CPUs", JOB_CPUS)])
    atf.require_slurm_running()


def _acct_name(key):
    return f"test_acct_130_7_{key}"


@pytest.fixture(scope="module")
def accrued_usage():
    """Create the accounts, accrue usage on both, then stop."""
    user = atf.get_user_name()

    for key, value in ACCTS.items():
        name = _acct_name(key)
        atf.run_command(
            f"sacctmgr -i add account {name}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        atf.run_command(
            f"sacctmgr -i add user {user} account={name}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        if value:
            atf.run_command(
                f"sacctmgr -i modify account {name} set TresDecayHalfLife={value}",
                user=atf.properties["slurm-user"],
                fatal=True,
            )

    job_ids = [
        atf.submit_job_sbatch(
            f"-A {_acct_name(key)} -N1 -n{JOB_CPUS} -t5 "
            f"--wrap 'sleep {JOB_RUN_SEC}'",
            fatal=True,
        )
        for key in ACCTS
    ]

    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "RUNNING", timeout=120, fatal=True)
    # Both must finish: an account still accruing would mask the hold below.
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "DONE", timeout=JOB_RUN_SEC + 120, fatal=True)

    yield

    atf.cancel_all_jobs()
    # The user first: one of these accounts is its default, and neither the
    # user nor the account can be removed while that is true.
    atf.run_command(
        f"sacctmgr -i remove user {user}",
        user=atf.properties["slurm-user"],
    )
    accts = ",".join(_acct_name(key) for key in ACCTS)
    atf.run_command(
        f"sacctmgr -i remove account {accts}",
        user=atf.properties["slurm-user"],
    )


@pytest.fixture(scope="module")
def decay_window(accrued_usage):
    """Sample both accounts before and after one shared stretch of decay."""
    sampled = {k: td.account_usage(_acct_name(k)) for k in ACCTS}
    before = {k: v[0] for k, v in sampled.items()}
    raw_before = {k: v[1] for k, v in sampled.items()}

    for key, value in before.items():
        assert value > 0, (
            f"{key} accrued no usage, so nothing below can mean anything; "
            f"the jobs did not run long enough or accounting is not recording"
        )

    # Close the window once the override has visibly moved. fatal= so a window
    # that never opens fails here, naming the cause, rather than surfacing as
    # an equality further down that could also mean the feature works.
    for _ in atf.timer(DECAY_WAIT_SEC, 10, quiet=True, fatal=True):
        if td.cpu_usage(_acct_name("override")) < before["override"]:
            break

    sampled = {k: td.account_usage(_acct_name(k)) for k in ACCTS}
    after = {k: v[0] for k, v in sampled.items()}
    raw_after = {k: v[1] for k, v in sampled.items()}

    return {
        "before": before,
        "after": after,
        "raw_before": raw_before,
        "raw_after": raw_after,
    }


def test_global_decay_is_off(decay_window):
    """With PriorityDecayHalfLife=0 an account with no override holds.

    This is the control. If it moved, the global decay is not actually off and
    nothing else in this module would be measuring what it claims to.
    """
    before = decay_window["before"]["global"]
    after = decay_window["after"]["global"]
    assert after == before, (
        f"PriorityDecayHalfLife=0 must leave GrpTRESMins usage alone, "
        f"moved from {before} to {after}"
    )


def test_override_still_decays(decay_window):
    """A TRES with its own half-life decays even with the global off.

    The decay thread skips a pass when the global factor is 1, which is what
    PriorityDecayHalfLife=0 produces. It may only skip when nothing asked for
    a rate of its own, so this account has to lose usage over a window the
    account above held through.
    """
    before = decay_window["before"]["override"]
    after = decay_window["after"]["override"]
    assert after < before, (
        f"cpu={OVERRIDE_HALF_LIFE_SEC} must decay even with "
        f"PriorityDecayHalfLife=0, went from {before} to {after}"
    )


def test_fairshare_does_not_decay(decay_window):
    """The override moves GrpTRESMins usage only, never RawUsage.

    A per-TRES half-life is for the limit counter. Fairshare stays on
    PriorityDecayHalfLife, which here is off, so RawUsage must hold on both
    accounts - including the one whose GrpTRESMins usage just decayed.
    """
    for key in ACCTS:
        before = decay_window["raw_before"][key]
        after = decay_window["raw_after"][key]
        assert after == pytest.approx(before), (
            f"{key} RawUsage must not decay with PriorityDecayHalfLife=0, "
            f"moved from {before} to {after}"
        )
