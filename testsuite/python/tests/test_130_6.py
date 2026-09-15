############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_130_6.py - per-TRES decay of GrpTRESMins usage.

Ticket 50920: TresDecayHalfLife sets how fast a TRES's GrpTRESMins usage
decays, overriding PriorityDecayHalfLife for that TRES only. 0 means it never
decays.

Four accounts accrue usage together and are then sampled twice, before and
after the same stretch of decay. Everything is asserted by comparing the
accounts against each other over that one shared window, never against a
decay curve computed from wall-clock time. That matters: decay lands in one
discrete step per PriorityCalcPeriod at a phase this test cannot see, so any
assertion built on elapsed time is comparing against a value the controller
never had. Comparing accounts that share a window cancels the phase out.

The ordering asserted below, frozen > slow > global > fast, is also what makes
the test fail if the feature is reverted: without it all four accounts decay
at PriorityDecayHalfLife and every fraction is equal.
"""

import pytest
import tres_decay as td

import atf

# 600s+: tox.ini defines the marker for anything over 60s.
pytestmark = pytest.mark.slow

# Several ticks per half-life, so decay arrives in steps small enough that a
# sample landing mid-tick cannot be mistaken for a different rate.
GLOBAL_HALF_LIFE = "00:05:00"
GLOBAL_HALF_LIFE_SEC = 300

FAST_HALF_LIFE_SEC = 60
SLOW_HALF_LIFE_SEC = 3000

PRIORITY_CALC_PERIOD_SEC = 60

# At least three PriorityCalcPeriod ticks while the jobs are still running, so
# the periodic decay pass has several chances to apply the running-job path's
# per-TRES half-life before any job ends, rather than possibly zero. Below
# this, test_running_job_accrual_decays_per_tres can tie on a healthy tree.
JOB_RUN_SEC = 3 * PRIORITY_CALC_PERIOD_SEC
DECAY_WAIT_SEC = 420

# scontrol reports GrpTRESMins usage in whole TRES-minutes, so the jobs have to
# accrue tens of minutes for the slowest half-life here to move the number at
# all. Usage accrues as cpus * seconds / 60, so a wide job reaches the same
# TRES-minutes in a fraction of the wall time; the nodes are Slurm's own
# configuration rather than the host's, so the width costs nothing. One node
# each, so they all run at once and share a window.
JOB_CPUS = 32

# A memory-requesting job, so mem accrues GrpTRESMins usage of its own and a
# mem-only override can be told apart from what cpu is doing on the same record.
JOB_MEM_MB = 1024

VERSION_REASON = "Ticket 50920: TresDecayHalfLife requires Slurm 26.11"

ACCTS = {
    "frozen": "cpu=0",
    "slow": f"cpu={SLOW_HALF_LIFE_SEC}",
    "global": None,
    "fast": f"cpu={FAST_HALF_LIFE_SEC}",
    # No override on the association: this account's job runs under a QOS that
    # carries the half-life, so what is measured is the QOS record.
    "nodecay": None,
    # Same, for a QOS frozen by a per-TRES half-life rather than by NoDecay.
    "qos_frozen": None,
    # Only mem is frozen here. cpu is left unset on purpose: the two TRES on
    # this one record have to move independently.
    "mem_frozen": "mem=0",
}

# NoDecay is a QOS flag and it suppresses the whole record, half-life included.
# The QOS gets the same rate as the "fast" account so the flag is the only
# difference between the two, and the fast account is what says the rate would
# otherwise have moved it.
NODECAY_QOS = "test_qos_130_6_nodecay"
NODECAY_ACCT_KEY = "nodecay"

# A QOS frozen by cpu=0 rather than by NoDecay. Both stop the record decaying
# but by different routes - the flag short-circuits the whole record, while a
# half-life of 0 makes the factor for that one TRES 1.0 - so both are worth
# holding onto.
FROZEN_QOS = "test_qos_130_6_frozen"
FROZEN_QOS_ACCT_KEY = "qos_frozen"

MEM_ACCT_KEY = "mem_frozen"


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
    # mem only accrues GrpTRESMins usage when it is a consumable resource.
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_accounting(modify=True)
    atf.require_nodes(len(ACCTS), [("CPUs", JOB_CPUS), ("RealMemory", JOB_MEM_MB)])
    atf.require_slurm_running()


def _acct_name(key):
    return f"test_acct_130_6_{key}"


@pytest.fixture(scope="module")
def accrued_usage():
    """Create the accounts, accrue usage on all of them, then stop.

    One accrual window shared by every test in the module: the jobs are what
    make this slow, and running them once is what keeps it to a few minutes
    rather than a few minutes per test.
    """
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

    atf.run_command(
        f"sacctmgr -i add qos {NODECAY_QOS} set flags=NoDecay "
        f"TresDecayHalfLife=cpu={FAST_HALF_LIFE_SEC}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i modify account {_acct_name(NODECAY_ACCT_KEY)} "
        f"set QosLevel={NODECAY_QOS}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {FROZEN_QOS} set TresDecayHalfLife=cpu=0",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i modify account {_acct_name(FROZEN_QOS_ACCT_KEY)} "
        f"set QosLevel={FROZEN_QOS}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    job_ids = []
    for key in ACCTS:
        # Each QOS is used by exactly one account, so only that QOS record
        # accrues usage and nothing else in the module has to know about it.
        extra = ""
        if key == NODECAY_ACCT_KEY:
            extra = f" --qos={NODECAY_QOS}"
        elif key == FROZEN_QOS_ACCT_KEY:
            extra = f" --qos={FROZEN_QOS}"
        elif key == MEM_ACCT_KEY:
            # Asking for memory is what makes mem accrue usage to freeze.
            extra = f" --mem={JOB_MEM_MB}"
        job_ids.append(
            atf.submit_job_sbatch(
                f"-A {_acct_name(key)} -N1 -n{JOB_CPUS} -t5{extra} "
                f"--wrap 'sleep {JOB_RUN_SEC}'",
                fatal=True,
            )
        )

    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "RUNNING", timeout=120, fatal=True)

    # Every account must finish. Without fatal= a stalled node leaves one
    # account still accruing, and a frozen account that grew instead of
    # holding would satisfy the assertion below for the wrong reason.
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
    # After the accounts, which still reference them until then.
    atf.run_command(
        f"sacctmgr -i remove qos {NODECAY_QOS},{FROZEN_QOS}",
        user=atf.properties["slurm-user"],
    )


@pytest.fixture(scope="module")
def decay_window(accrued_usage):
    """Sample every account before and after one shared stretch of decay.

    Returns the GrpTRESMins cpu usage of every account as "before" and
    "after", and its RawUsage as "raw_before" and "raw_after".
    """
    sampled = {k: td.account_usage(_acct_name(k)) for k in ACCTS}
    before = {k: v[0] for k, v in sampled.items()}
    raw_before = {k: v[1] for k, v in sampled.items()}
    qos_before = td.qos_cpu_usage(NODECAY_QOS)
    frozen_qos_before = td.qos_cpu_usage(FROZEN_QOS)
    mem_before = td.mem_usage(_acct_name(MEM_ACCT_KEY))

    for key, value in before.items():
        assert value > 0, (
            f"{key} accrued no usage, so nothing below can mean anything; "
            f"the jobs did not run long enough or accounting is not recording"
        )

    assert qos_before > 0, (
        f"{NODECAY_QOS} accrued no usage, so it holding below would mean "
        f"nothing; the job did not run under the QOS"
    )
    assert frozen_qos_before > 0, (
        f"{FROZEN_QOS} accrued no usage, so it holding below would mean "
        f"nothing; the job did not run under the QOS"
    )
    assert mem_before > 0, (
        f"the {MEM_ACCT_KEY} account accrued no mem usage, so it holding below "
        f"would mean nothing; the job did not request memory, or mem is not a "
        f"consumable resource"
    )

    # Close the window once the slow account has visibly decayed. It is the
    # slowest of the three that must decay, so by then the other two have
    # moved further, and usage is reported in whole TRES-minutes, so ending
    # any earlier would leave the slow account rounding to where it started.
    # fatal= so a window that never opens fails here, naming the cause,
    # rather than surfacing as a tie in the ordering assertions below.
    for _ in atf.timer(DECAY_WAIT_SEC, 10, quiet=True, fatal=True):
        if td.cpu_usage(_acct_name("slow")) < before["slow"]:
            break

    sampled = {k: td.account_usage(_acct_name(k)) for k in ACCTS}
    after = {k: v[0] for k, v in sampled.items()}
    raw_after = {k: v[1] for k, v in sampled.items()}
    qos_after = td.qos_cpu_usage(NODECAY_QOS)
    frozen_qos_after = td.qos_cpu_usage(FROZEN_QOS)
    mem_after = td.mem_usage(_acct_name(MEM_ACCT_KEY))

    return {
        "before": before,
        "after": after,
        "raw_before": raw_before,
        "raw_after": raw_after,
        "qos_before": qos_before,
        "qos_after": qos_after,
        "frozen_qos_before": frozen_qos_before,
        "frozen_qos_after": frozen_qos_after,
        "mem_before": mem_before,
        "mem_after": mem_after,
    }


def _fraction(window, key):
    """How much of the accrued usage survived the window."""
    return window["after"][key] / window["before"][key]


@pytest.mark.parametrize("key", list(ACCTS))
def test_controller_has_the_half_life(accrued_usage, key):
    """The controller reports what the database was told.

    The decay happens in the controller, so a value that reached the
    database but not the controller would leave every account below decaying
    at PriorityDecayHalfLife with nothing to say so.
    """
    got = td.reported_half_life(_acct_name(key))
    assert got == (
        ACCTS[key] or ""
    ), f"{key} was set to {ACCTS[key]}, controller reports {got!r}"


def test_frozen_tres_does_not_decay(decay_window):
    """cpu=0 means that TRES's GrpTRESMins usage never decays."""
    before = decay_window["before"]["frozen"]
    after = decay_window["after"]["frozen"]
    assert after == before, (
        f"frozen account moved from {before} to {after}; "
        f"cpu=0 should hold usage exactly where it is"
    )


def test_running_job_accrual_decays_per_tres(decay_window):
    """frozen and fast have separated by the time decay_window opens.

    GrpTRESMins usage is folded in as a job runs, not only when it ends, so
    _calc_tres_run_decay() has to apply the per-TRES half-life to live
    accrual the same way the periodic pass does. JOB_RUN_SEC gives the
    periodic pass several ticks to fire while the jobs are still running, so
    it has repeated chances to apply that path before decay_window samples
    "before". This does not isolate the running-job path by itself: the
    separation it measures is whatever that path did while the jobs were up,
    plus whatever the periodic pass did in the gap between the jobs ending
    and the sample being taken. It is that combination, not the running-job
    path alone, that fails if _calc_tres_run_decay() is reverted.

    Only the frozen/fast pair is compared: they are ~5x apart, where frozen
    and slow differ by ~1.4% per pass and would be sensitive to job start
    skew.
    """
    frozen = decay_window["before"]["frozen"]
    fast = decay_window["before"]["fast"]
    assert frozen > fast, (
        f"identical jobs accrued {frozen} on cpu=0 and {fast} on "
        f"cpu={FAST_HALF_LIFE_SEC}; a running job's usage should be decayed "
        f"with that TRES's own half-life as it accrues"
    )


def test_unset_tres_follows_the_global(decay_window):
    """An account with no half-life of its own still decays."""
    before = decay_window["before"]["global"]
    after = decay_window["after"]["global"]
    assert after < before, (
        f"account with no override held at {after}; it should decay at "
        f"PriorityDecayHalfLife"
    )


def test_rate_ordering(decay_window):
    """A shorter half-life decays more of the usage over the same window.

    This is the assertion that fails if the feature is reverted: without it
    every account decays at PriorityDecayHalfLife and all four fractions are
    equal, so no strict ordering can hold.
    """
    fast = _fraction(decay_window, "fast")
    glob = _fraction(decay_window, "global")
    slow = _fraction(decay_window, "slow")
    frozen = _fraction(decay_window, "frozen")

    assert fast < glob, (
        f"cpu={FAST_HALF_LIFE_SEC} should decay more than the "
        f"{GLOBAL_HALF_LIFE_SEC}s global: kept {fast:.3f} vs {glob:.3f}"
    )
    assert glob < slow, (
        f"the {GLOBAL_HALF_LIFE_SEC}s global should decay more than "
        f"cpu={SLOW_HALF_LIFE_SEC}: kept {glob:.3f} vs {slow:.3f}"
    )
    assert slow < frozen, (
        f"cpu={SLOW_HALF_LIFE_SEC} should decay more than cpu=0: "
        f"kept {slow:.3f} vs {frozen:.3f}"
    )


def test_fairshare_still_decays_on_a_frozen_account(decay_window):
    """Freezing a TRES must not freeze fairshare with it.

    This is the whole point of the feature: PriorityDecayHalfLife=0 would
    have stopped both.
    """
    before = decay_window["raw_before"]["frozen"]
    after = decay_window["raw_after"]["frozen"]
    assert after < before, (
        f"RawUsage held at {after} on the frozen account; only GrpTRESMins "
        f"usage should stop decaying"
    )


def test_nodecay_qos_overrides_the_half_life(decay_window):
    """NoDecay on a QOS suppresses the half-life the QOS itself carries.

    The QOS has the same cpu half-life as the "fast" account, so the fast
    account decaying over this same window is what says the rate would have
    moved the QOS too. NoDecay is the only difference between them.
    """
    before = decay_window["qos_before"]
    after = decay_window["qos_after"]

    fast_before = decay_window["before"]["fast"]
    fast_after = decay_window["after"]["fast"]
    assert fast_after < fast_before, (
        f"cpu={FAST_HALF_LIFE_SEC} did not decay on the association either, so "
        f"the QOS holding says nothing about NoDecay"
    )

    assert after == before, (
        f"NoDecay must suppress the QOS's own cpu={FAST_HALF_LIFE_SEC} "
        f"half-life, but usage moved from {before} to {after}"
    )


def test_qos_half_life_of_zero_freezes_the_qos(decay_window):
    """cpu=0 on a QOS freezes that QOS's GrpTRESMins usage.

    The same value on an association is covered above. This is the QOS route
    into the same code, and it is a different route from NoDecay: the flag
    short-circuits the whole record, while a half-life of 0 makes the factor
    for that one TRES 1.0.
    """
    before = decay_window["frozen_qos_before"]
    after = decay_window["frozen_qos_after"]
    assert after == before, (
        f"cpu=0 on a QOS must stop its GrpTRESMins usage decaying, but it "
        f"moved from {before} to {after}"
    )


def test_freezing_one_tres_leaves_the_other_alone(decay_window):
    """mem=0 freezes mem on a record whose cpu is still on the global rate.

    Both TRES live in the same usage_tres_raw array on the same association, so
    this is what says the array is walked per TRES rather than as a whole. cpu
    decaying here is the control: if it held too, the record stopped decaying
    for some other reason than the mem override.
    """
    key = MEM_ACCT_KEY
    mem_before = decay_window["mem_before"]
    mem_after = decay_window["mem_after"]
    cpu_before = decay_window["before"][key]
    cpu_after = decay_window["after"][key]

    assert cpu_after < cpu_before, (
        f"cpu is unset on this account and must follow the "
        f"{GLOBAL_HALF_LIFE_SEC}s global, but it held at {cpu_after}; the mem "
        f"hold below would then say nothing about per-TRES independence"
    )
    assert mem_after == mem_before, (
        f"mem=0 must stop mem decaying, but it moved from {mem_before} to "
        f"{mem_after} while cpu went {cpu_before} -> {cpu_after}"
    )
