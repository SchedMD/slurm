############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_130_8.py - a frozen GrpTRESMins quota keeps blocking jobs.

Ticket 50920: TresDecayHalfLife=cpu=0 stops GrpTRESMins cpu usage decaying,
which turns GrpTRESMins from a rolling window into a quota that does not
replenish.

The sibling modules read usage numbers out of the controller. This one asserts
what an admin actually sets the value for: once a frozen account is over its
GrpTRESMins limit it stays over, so its jobs stay pending, while an account on
a real half-life has the same usage decay back under the limit and its jobs
start. Nothing else covers the enforcement end of the feature.

Both accounts accrue the same usage from the same shaped job and hold the same
limit, so the half-life is the only difference between them.
"""

import pytest

import atf

# 280s: tox.ini defines the marker for anything over 60s.
pytestmark = pytest.mark.slow

FAST_HALF_LIFE = "00:01:00"
FAST_HALF_LIFE_SEC = 60

# One job of JOB_CPUS for JOB_RUN_SEC accrues JOB_CPUS * JOB_RUN_SEC / 60
# cpu-minutes, so 16 here. The limit has to sit under that for the first job to
# put the account over it, and far enough under that a sample landing mid-tick
# cannot make an over-limit account look under. Usage accrues as
# cpus * seconds / 60, so a wide job reaches the same TRES-minutes in a fraction
# of the wall time; the nodes are Slurm's own configuration rather than the
# host's, so the width costs nothing.
JOB_CPUS = 32
JOB_RUN_SEC = 30
GRP_TRES_MINS_CPU_LIMIT = 8

# From 16 cpu-minutes, one 60s half-life reaches 8 - still at the limit, so
# still blocked - and the next reaches 4, so the account on a half-life is
# released after two ticks. Three is enough to wait, and enough to hold the
# frozen one through.
RELEASE_WAIT_SEC = 180

VERSION_REASON = "Ticket 50920: TresDecayHalfLife requires Slurm 26.11"
BLOCK_REASON = "AssocGrpCPUMinutesLimit"

ACCTS = {
    "frozen": "cpu=0",
    "decaying": f"cpu={FAST_HALF_LIFE_SEC}",
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((26, 11), "bin/sacctmgr", reason=VERSION_REASON)
    atf.require_version((26, 11), "sbin/slurmdbd", reason=VERSION_REASON)
    atf.require_version((26, 11), "sbin/slurmctld", reason=VERSION_REASON)
    atf.require_auto_config("sets PriorityDecayHalfLife and a GrpTRESMins limit")
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    atf.require_config_parameter("PriorityCalcPeriod", 1)
    atf.require_config_parameter("PriorityDecayHalfLife", FAST_HALF_LIFE)
    atf.require_config_parameter("PriorityUsageResetPeriod", "NONE")
    # Without limits enforced nothing is blocked and the module asserts nothing.
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_accounting(modify=True)
    atf.require_nodes(len(ACCTS), [("CPUs", JOB_CPUS)])
    atf.require_slurm_running()


def _acct_name(key):
    return f"test_acct_130_8_{key}"


def _submit(key, run_sec=JOB_RUN_SEC):
    return atf.submit_job_sbatch(
        f"-A {_acct_name(key)} -N1 -n{JOB_CPUS} -t5 " f"--wrap 'sleep {run_sec}'",
        fatal=True,
    )


@pytest.fixture(scope="module")
def over_limit():
    """Put both accounts over the same GrpTRESMins cpu limit, then stop."""
    user = atf.get_user_name()

    for key, value in ACCTS.items():
        name = _acct_name(key)
        atf.run_command(
            f"sacctmgr -i add account {name} "
            f"set GrpTRESMins=cpu={GRP_TRES_MINS_CPU_LIMIT} "
            f"TresDecayHalfLife={value}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        atf.run_command(
            f"sacctmgr -i add user {user} account={name}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    # Usage starts at 0, so this first job is under the limit and runs; when it
    # finishes both accounts are over it.
    job_ids = [_submit(key) for key in ACCTS]
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "RUNNING", timeout=120, fatal=True)
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "DONE", timeout=JOB_RUN_SEC + 120, fatal=True)

    yield

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


def test_decaying_account_is_released(over_limit):
    """An account on a real half-life decays back under the limit and runs.

    The control. Both accounts are over the limit by the same amount, so this
    running is what says the limit is enforced against decaying usage and that
    the block below is about the half-life rather than about the limit never
    being reachable.
    """
    job_id = _submit("decaying", run_sec=5)
    atf.wait_for_job_state(job_id, "RUNNING", timeout=RELEASE_WAIT_SEC, fatal=True)


def test_frozen_account_stays_blocked(over_limit):
    """A cpu=0 account stays over its limit, so its job stays pending.

    Held for the same stretch that released the decaying account above, and
    checked for the specific reason: a job pending for any other reason would
    otherwise pass this.
    """
    job_id = _submit("frozen", run_sec=5)
    atf.wait_for_job_state(
        job_id, "PENDING", desired_reason=BLOCK_REASON, timeout=120, fatal=True
    )

    # Long enough that the decaying account had been released by now. Reaching
    # the timeout is the pass condition here, so xfail keeps atf.timer() from
    # logging "Timer should not timeout" on every successful run.
    for _ in atf.timer(RELEASE_WAIT_SEC, 15, xfail=True, quiet=True):
        state = atf.get_job_parameter(job_id, "JobState", quiet=True)
        assert state == "PENDING", (
            f"a cpu=0 account's GrpTRESMins usage never decays, so the job "
            f"must stay pending, but it reached {state}"
        )


def test_resetting_usage_releases_the_frozen_account(over_limit):
    """Zeroing the usage is what releases a frozen quota.

    The usage is the only thing holding the job, so clearing it has to start
    the job. This is also the operational escape hatch for a quota that by
    design never replenishes on its own.

    Must stay last in the module: it clears the usage the fixture accrued, so
    anything asserting the frozen account is still over its limit has to run
    before it.
    """
    job_id = _submit("frozen", run_sec=5)
    atf.wait_for_job_state(
        job_id, "PENDING", desired_reason=BLOCK_REASON, timeout=120, fatal=True
    )

    atf.run_command(
        f"sacctmgr -i modify account {_acct_name('frozen')} set RawUsage=0",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "RUNNING", timeout=RELEASE_WAIT_SEC, fatal=True)
