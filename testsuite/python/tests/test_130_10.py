############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_130_10.py - a PriorityDecayHalfLife reconfigure leaves overrides alone.

Ticket 50920: a TRES with no TresDecayHalfLife of its own follows
PriorityDecayHalfLife, and one with an override does not.

That distinction has to survive PriorityDecayHalfLife changing under a running
controller. A `scontrol reconfigure` re-execs slurmctld, which rebuilds every
per-TRES array from scratch by pulling associations and QoS fresh from
slurmdbd, so this module cannot tell whether those arrays store NO_VAL64
sentinels resolved at use time or the global itself -- either would rebuild
identically across the re-exec. What it does show is the observable behaviour:
an unset TRES follows PriorityDecayHalfLife to its new value while an
overridden TRES keeps its own rate. Usage counters are what make the
before/after comparison legitimate here: they survive the re-exec through
save_all_state()/load_assoc_usage() rather than being rebuilt from the
database, so a real gap in decay across the reconfigure would show up as one.

Two accounts start on the same rate - one because it is the global, one because
its override matches it - so the first window shows them moving together. The
global is then reconfigured far slower and the second window has to separate
them, with the override still moving and the unset account nearly stopped.

Compared within a window rather than against a decay curve: decay lands in one
discrete step per PriorityCalcPeriod at a phase this test cannot see, and usage
is reported in whole TRES-minutes, so a fraction kept is the only thing two
accounts can be held to.
"""

import pytest
import tres_decay as td

import atf

# 390s+: tox.ini defines the marker for anything over 60s.
pytestmark = pytest.mark.slow

FAST_HALF_LIFE = "00:01:00"
FAST_HALF_LIFE_SEC = 60

# Far enough from the fast rate that no window length could confuse the two.
SLOW_HALF_LIFE = "16:40:00"

# Big enough that the whole-TRES-minute rounding in the report is a small part
# of what is being compared, in the second window as well as the first - the
# second starts from whatever the first left behind. Usage accrues as
# cpus * seconds / 60, so a wide job reaches the same TRES-minutes in a fraction
# of the wall time; the nodes are Slurm's own configuration rather than the
# host's, so the width costs nothing.
JOB_CPUS = 32
JOB_RUN_SEC = 90
DECAY_WAIT_SEC = 300

VERSION_REASON = "Ticket 50920: TresDecayHalfLife requires Slurm 26.11"

UNSET_KEY = "unset"
OVERRIDE_KEY = "override"
ACCTS = {
    # Follows whatever PriorityDecayHalfLife happens to be.
    UNSET_KEY: None,
    # Starts out matching the global, so the two only separate once it changes.
    OVERRIDE_KEY: f"cpu={FAST_HALF_LIFE_SEC}",
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((26, 11), "bin/sacctmgr", reason=VERSION_REASON)
    atf.require_version((26, 11), "sbin/slurmdbd", reason=VERSION_REASON)
    atf.require_version((26, 11), "sbin/slurmctld", reason=VERSION_REASON)
    atf.require_auto_config("reconfigures PriorityDecayHalfLife mid-test")
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    atf.require_config_parameter("PriorityCalcPeriod", 1)
    atf.require_config_parameter("PriorityDecayHalfLife", FAST_HALF_LIFE)
    atf.require_config_parameter("PriorityUsageResetPeriod", "NONE")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_accounting(modify=True)
    atf.require_nodes(len(ACCTS), [("CPUs", JOB_CPUS)])
    atf.require_slurm_running()


def _acct_name(key):
    return f"test_acct_130_10_{key}"


def _sample():
    return {k: td.cpu_usage(_acct_name(k)) for k in ACCTS}


def _wait_for_drop(key, baseline):
    """Close a window once key has visibly moved, so both are read after it."""
    for _ in atf.timer(DECAY_WAIT_SEC, 10, quiet=True, fatal=True):
        if td.cpu_usage(_acct_name(key)) < baseline:
            return


@pytest.fixture(scope="module")
def windows():
    """One window on the original global, one after reconfiguring it slower."""
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
            f"-A {_acct_name(key)} -N1 -n{JOB_CPUS} -t8 "
            f"--wrap 'sleep {JOB_RUN_SEC}'",
            fatal=True,
        )
        for key in ACCTS
    ]
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "RUNNING", timeout=120, fatal=True)
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "DONE", timeout=JOB_RUN_SEC + 180, fatal=True)

    first_before = _sample()
    for key, value in first_before.items():
        assert value > 0, (
            f"{key} accrued no usage, so nothing below can mean anything; "
            f"the job did not run long enough or accounting is not recording"
        )
    _wait_for_drop(UNSET_KEY, first_before[UNSET_KEY])
    first_after = _sample()

    # Reconfigured, not restarted: the point is that a running controller keeps
    # the override while the unset account moves to the new global.
    atf.set_config_parameter("PriorityDecayHalfLife", SLOW_HALF_LIFE)

    second_before = _sample()
    _wait_for_drop(OVERRIDE_KEY, second_before[OVERRIDE_KEY])
    second_after = _sample()

    yield {
        "first_before": first_before,
        "first_after": first_after,
        "second_before": second_before,
        "second_after": second_after,
    }

    atf.cancel_all_jobs()
    atf.run_command(
        f"sacctmgr -i remove user {user}",
        user=atf.properties["slurm-user"],
    )
    accts = ",".join(_acct_name(key) for key in ACCTS)
    atf.run_command(
        f"sacctmgr -i remove account {accts}",
        user=atf.properties["slurm-user"],
    )


def _kept(windows, phase, key):
    """How much of the usage survived one window."""
    return windows[f"{phase}_after"][key] / windows[f"{phase}_before"][key]


def test_both_decay_on_the_same_rate_first(windows):
    """Before the reconfigure both accounts are on the same half-life.

    The setup control: the override matches the global to start with, so if
    these two already differed, the separation asserted below would say nothing
    about the reconfigure.
    """
    for key in ACCTS:
        before = windows["first_before"][key]
        after = windows["first_after"][key]
        assert after < before, (
            f"{key} should decay on {FAST_HALF_LIFE} before the reconfigure, "
            f"but held at {after}"
        )


def test_reconfigure_moves_the_unset_account(windows):
    """An unset TRES follows PriorityDecayHalfLife to its new value.

    Its own two windows: it decayed on the fast rate before, and has to nearly
    stop once the global is far slower. The gap between the two rates is wide
    enough that the windows differing in length cannot account for it.
    """
    first = _kept(windows, "first", UNSET_KEY)
    second = _kept(windows, "second", UNSET_KEY)
    assert second > first, (
        f"the unset account kept {second:.3f} after the reconfigure to "
        f"{SLOW_HALF_LIFE} and {first:.3f} before it on {FAST_HALF_LIFE}; it "
        f"is not following the global"
    )


def test_override_ignores_the_reconfigure(windows):
    """An overridden TRES keeps its own rate across the reconfigure.

    Both accounts are compared over the one window after the reconfigure, so
    the tick phase cancels: the override has to keep less of its usage than the
    account that just moved to the slow global.
    """
    override = _kept(windows, "second", OVERRIDE_KEY)
    unset = _kept(windows, "second", UNSET_KEY)
    assert override < unset, (
        f"after the reconfigure the cpu={FAST_HALF_LIFE_SEC} override kept "
        f"{override:.3f} and the unset account kept {unset:.3f}; the override "
        f"should still be the faster of the two"
    )
