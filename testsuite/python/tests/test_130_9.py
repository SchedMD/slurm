############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_130_9.py - setting TresDecayHalfLife takes effect without a restart.

Ticket 50920: the per-TRES arrays the decay thread reads are rebuilt when an
association is modified, so a value set with sacctmgr applies on the next decay
pass rather than at the next slurmctld start.

The sibling modules set the value before any usage exists, which never exercises
the update path: an array built at load time would satisfy them all. Here both
accounts accrue usage with no override at all, and one is frozen afterwards, so
the only way it can hold is if the modify reached the running controller.

Both accounts are sampled over one window that starts after the modify, so the
account left alone is the control: it has to keep decaying over exactly the
stretch the frozen one holds through.
"""

import pytest
import tres_decay as td

import atf

# 340s+: tox.ini defines the marker for anything over 60s.
pytestmark = pytest.mark.slow

# Fast, so the control moves a whole TRES-minute inside a short window.
GLOBAL_HALF_LIFE = "00:01:00"

# Usage accrues as cpus * seconds / 60, so a wide job reaches the same
# TRES-minutes in a fraction of the wall time; the nodes are Slurm's own
# configuration rather than the host's, so the width costs nothing.
JOB_CPUS = 32
JOB_RUN_SEC = 40
DECAY_WAIT_SEC = 300

VERSION_REASON = "Ticket 50920: TresDecayHalfLife requires Slurm 26.11"

# Both start with no override; only one is frozen after the usage exists.
FROZEN_KEY = "frozen_late"
CONTROL_KEY = "control"
ACCTS = (FROZEN_KEY, CONTROL_KEY)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((26, 11), "bin/sacctmgr", reason=VERSION_REASON)
    atf.require_version((26, 11), "sbin/slurmdbd", reason=VERSION_REASON)
    atf.require_version((26, 11), "sbin/slurmctld", reason=VERSION_REASON)
    atf.require_auto_config("sets PriorityDecayHalfLife and PriorityCalcPeriod")
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    atf.require_config_parameter("PriorityCalcPeriod", 1)
    atf.require_config_parameter("PriorityDecayHalfLife", GLOBAL_HALF_LIFE)
    atf.require_config_parameter("PriorityUsageResetPeriod", "NONE")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_accounting(modify=True)
    atf.require_nodes(len(ACCTS), [("CPUs", JOB_CPUS)])
    atf.require_slurm_running()


def _acct_name(key):
    return f"test_acct_130_9_{key}"


@pytest.fixture(scope="module")
def window():
    """Accrue usage with no override, freeze one account, then sample."""
    user = atf.get_user_name()

    for key in ACCTS:
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
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "DONE", timeout=JOB_RUN_SEC + 120, fatal=True)

    # Nothing was set before the usage existed: that is the point.
    for key in ACCTS:
        assert not td.reported_half_life(_acct_name(key)), (
            f"{key} already reports a half-life before the modify, so this "
            f"module would not be exercising the update path"
        )

    atf.run_command(
        f"sacctmgr -i modify account {_acct_name(FROZEN_KEY)} "
        f"set TresDecayHalfLife=cpu=0",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # sacctmgr returning only means slurmdbd committed the change; the
    # controller applies it once the update RPC lands. Sample "before" only
    # once the controller has it, so a decay pass firing in that gap cannot
    # be mistaken for the frozen account failing to hold.
    for _ in atf.timer(60, 2, quiet=True, fatal=True):
        if td.reported_half_life(_acct_name(FROZEN_KEY)) == "cpu=0":
            break

    before = {k: td.cpu_usage(_acct_name(k)) for k in ACCTS}
    for key, value in before.items():
        assert value > 0, (
            f"{key} accrued no usage, so nothing below can mean anything; "
            f"the job did not run long enough or accounting is not recording"
        )

    # Close the window once the control has visibly decayed, so the frozen
    # account is asserted to hold over a stretch that demonstrably decays.
    for _ in atf.timer(DECAY_WAIT_SEC, 10, quiet=True, fatal=True):
        if td.cpu_usage(_acct_name(CONTROL_KEY)) < before[CONTROL_KEY]:
            break

    after = {k: td.cpu_usage(_acct_name(k)) for k in ACCTS}

    yield {"before": before, "after": after}

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


def test_controller_picks_up_the_modify(window):
    """The value set after the account existed reaches the controller."""
    assert td.reported_half_life(_acct_name(FROZEN_KEY)) == "cpu=0", (
        f"the controller still reports "
        f"{td.reported_half_life(_acct_name(FROZEN_KEY))!r} for "
        f"{_acct_name(FROZEN_KEY)}"
    )


def test_control_keeps_decaying(window):
    """The account left alone keeps decaying on the global half-life.

    The control for the assertion below: it establishes that the window really
    did contain decay passes.
    """
    before = window["before"][CONTROL_KEY]
    after = window["after"][CONTROL_KEY]
    assert after < before, (
        f"the untouched account should follow {GLOBAL_HALF_LIFE}, but held at "
        f"{after}"
    )


def test_live_freeze_takes_effect(window):
    """cpu=0 set after the usage existed stops that usage decaying."""
    before = window["before"][FROZEN_KEY]
    after = window["after"][FROZEN_KEY]
    assert after == before, (
        f"cpu=0 set with sacctmgr must apply without a restart, but usage "
        f"moved from {before} to {after}"
    )
