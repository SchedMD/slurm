############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Tests for TRESRunMins usage when --time-min job time limits are trimmed."""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# Global variables
account = "acct1"
qos1 = "qos1"
resv1 = "resv1"
test_user = atf.properties["test-user"]
# Filled in setup() with the two node names the partition is pinned to.
part_nodes = None

# The victim job asks for job_time_limit but will settle for job_time_min. Every
# other limit here is picked so that the limit the victim ends up with
# identifies which code path trimmed it.
job_time_limit = 60
job_time_min = 1
reserving_time_limit = 30
# What backfill's node_space map trims the victim down to.
blocker_time_limit = 20

# The reservation test's blocker outlasts resv_start_minutes but not
# job_time_limit, so backfill's node_space map trims to about
# resv_blocker_time_limit while the reservation trims further, to
# resv_start_minutes. Only the reservation can produce the limit that test
# asserts on.
resv_blocker_time_limit = 30
resv_start_minutes = 10
resv_duration_minutes = 600

# Trimming is to whole minutes remaining, so the trimmed limit is short by
# however long the test took to get the victim running.
trim_tolerance = 5

# Enforcement test limit, in cpu-minutes; every job here is exclusive on a two
# CPU node. It has to stay above what the whole-cluster job is tested against
# (blocker 2*20 + victim 2*60 + 4*30 = 280), or that job gets rejected by the
# limit instead of parked on the busy node and never reserves anything.
run_mins_limit = 300
# Fits under the limit, but not once the 2*(60-20) an unfixed slurmctld leaks is
# added to it.
probe_time_limit = 130
# Over the limit on its own.
oversized_probe_time_limit = 200


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""
    atf.require_version(
        (26, 5, 5),
        "sbin/slurmctld",
        reason="Ticket 25339 TRESRunMins give-back when a job's time limit is "
        "trimmed is fixed in 26.05.5",
    )
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    # Default to multifactor so PriorityCalcPeriod can suspend the decay thread
    # for the tests that do not pick a plugin of their own. The parametrized
    # backfill trim test also runs under priority/basic, which has no decay
    # thread and therefore needs no such knob.
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    # Keep the decay thread from lowering usage between snapshots.
    atf.require_config_parameter("PriorityCalcPeriod", 9999)
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    # Use includes so that any existing SchedulerParameters are preserved.
    # These use the (key, value) form so that a value already set by the
    # config being tested against is REPLACED rather than appended: Slurm
    # honors the first occurrence of a key, so an appended duplicate is
    # silently ignored.
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 5))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", "bf_continue")
    # The victim is trimmed against the future allocation backfill reserves for
    # the pending whole-cluster job. bf_min_prio_reserve suppresses that
    # reservation for jobs below its threshold, and also lets the victim start
    # immediately instead of being backfilled, so nothing trims it.
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_min_prio_reserve", 0)
    )
    # bf_window has to reach past the victim's requested limit, or backfill
    # never plans far enough ahead to see the conflict.
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_window", 120))
    # node_space entries snap to bf_resolution, so it has to be well under
    # trim_tolerance for the trimmed limit to be assertable.
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_resolution", 60))
    atf.require_nodes(2, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_slurm_running()
    # require_nodes(2) only guarantees at least two nodes. Nodes=ALL would let
    # the -N2 --exclusive reserving job start on extra idle nodes instead of
    # staying PENDING, so backfill would never build the node_space reservation
    # that trims the victim.
    global part_nodes
    part_nodes = list(atf.get_nodes().keys())[:2]
    assert len(part_nodes) == 2, f"Need exactly two partition nodes, got {part_nodes}"
    atf.require_config_parameter(
        "PartitionName",
        {
            "part1": {
                "Nodes": ",".join(part_nodes),
                "Default": "YES",
                "MaxTime": "INFINITE",
            }
        },
    )


@pytest.fixture(scope="function")
def slurm_priority_type(request):
    """Switch PriorityType when a test parametrizes this fixture.

    Tests that do not parametrize it keep the module default (multifactor).
    priority/basic has no decay thread, so PriorityCalcPeriod is only restored
    when switching back to multifactor.

    The restart must be clean. Recovering running jobs across a plugin change
    re-runs acct_policy_job_begin() without multifactor's elapsed-usage init,
    which leaves TRESRunMins booked after those jobs are gone.
    """

    wanted = request.param if hasattr(request, "param") else "priority/multifactor"
    current = atf.get_config_parameter("PriorityType", live=False, quiet=True)
    if str(current).casefold() != wanted.casefold():
        atf.cancel_all_jobs(quiet=True)
        atf.require_config_parameter("PriorityType", wanted)
        if wanted == "priority/multifactor":
            atf.require_config_parameter("PriorityCalcPeriod", 9999)
        atf.restart_slurm(clean=True)
    return wanted


@pytest.fixture(scope="function", autouse=True)
def setup_db(slurm_priority_type):
    atf.run_command(
        f"sacctmgr -i add qos {qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {account}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {test_user} account={account} qos=normal,{qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"scontrol update partitionname=part1 qos={qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.cancel_all_jobs(quiet=True)
    atf.run_command(
        f"scontrol delete reservationname={resv1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        "scontrol update partitionname=part1 qos=",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove user {test_user} where account={account}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {account}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove qos {qos1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def time_limit_minutes(job_id):
    """Return a job's TimeLimit in minutes."""

    time_limit = atf.get_job_parameter(job_id, "TimeLimit")
    assert time_limit is not None, f"Job {job_id} should still report a TimeLimit"
    match = re.match(r"(?:(\d+)-)?(\d+):(\d+):(\d+)$", time_limit)
    assert match is not None, f"Unable to parse TimeLimit ({time_limit})"
    days, hours, minutes, seconds = (int(g or 0) for g in match.groups())
    return ((days * 24 + hours) * 60) + minutes + (seconds / 60)


def used_run_mins(entity, limit="GrpTRESRunMins"):
    """Return the non-zero usage the qos or the test assoc booked for limit.

    GrpTRESRunMins and MaxTRESRunMinsPU are separate counters that slurmctld
    maintains independently, so they are never reported together. Folding them
    would hide an over-removal in one behind the other's larger value.
    """

    if entity == "qos":
        command = f"scontrol show assoc_mgr qos={qos1} flags=qos"
    else:
        command = f"scontrol show assoc_mgr flags=assoc user={test_user}"
    output = atf.run_command_output(
        command, user=atf.properties["slurm-user"], fatal=True
    )

    usage = {}
    for line in output.splitlines():
        line = line.strip()
        if not line.startswith(f"{limit}="):
            continue
        for tres, used in re.findall(r"([\w/:]+)=[^=,]*?\((\d+)\)", line):
            usage[tres] = max(usage.get(tres, 0), int(used))
    return {tres: used for tres, used in usage.items() if used}


def get_test_nodes():
    """Return the two node names the partition is pinned to."""

    return part_nodes[0], part_nodes[1]


def submit_blocker(node, time_limit):
    """Start a job holding a whole node, so the job below cannot start."""

    job_id = atf.submit_job_sbatch(
        f"-w {node} --exclusive -t{time_limit} -A {account} --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), "Blocker job should be running"
    return job_id


def submit_reserving_job():
    """Submit the whole-cluster job that backfill has to plan around.

    While it waits it both makes the main scheduler give up on the partition
    and gives backfill a future allocation to trim the victim against.
    """

    job_id = atf.submit_job_sbatch(
        f"-N2 --exclusive -t{reserving_time_limit} -A {account} "
        f"--wrap 'sleep infinity'",
        fatal=True,
    )
    # PENDING is already true right after submit, so wait for a Reason instead.
    reason = None
    for _ in atf.timer(timeout=60):
        reason = atf.get_job_parameter(job_id, "Reason")
        if reason not in (None, "", "None"):
            break
    assert reason not in (
        None,
        "",
        "None",
    ), "Reserving job should have been evaluated and blocked by the scheduler"

    # Guards the enforcement test below: parked on the limit rather than on the
    # busy node means nothing was reserved for the victim to be trimmed against.
    assert "RunMinutes" not in reason, (
        f"Reserving job was blocked by a TRESRunMins limit ({reason}) instead "
        f"of by the busy node, so it never reserved the nodes the victim has "
        f"to be trimmed against. run_mins_limit is too low."
    )
    return job_id


def submit_victim(node):
    """Submit the --time-min job whose time limit is trimmed after it starts."""

    job_id = atf.submit_job_sbatch(
        f"-w {node} --exclusive -t{job_time_limit} --time-min={job_time_min} "
        f"-A {account} --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", timeout=150, fatal=True
    ), "Backfilled job should be running"
    assert atf.get_job_parameter(job_id, "QOS") == "normal", (
        f"The victim must hold normal as its job QOS so that {qos1} is reached "
        f"only as the partition QOS; one shared QOS would stop these tests "
        f"covering the partition QOS give-back"
    )
    return job_id


def assert_trimmed_by(job_id, expected, source):
    """Assert a job's time limit was trimmed to expected, naming what did it."""

    trimmed_limit = time_limit_minutes(job_id)
    assert trimmed_limit >= job_time_min, (
        f"{source} must not trim the time limit below --time-min "
        f"({job_time_min}), but it is {trimmed_limit}"
    )
    assert expected - trim_tolerance <= trimmed_limit <= expected, (
        f"{source} should have trimmed the time limit to about {expected} "
        f"minutes, but it is {trimmed_limit}. A limit of {job_time_limit} means "
        f"nothing trimmed the job, {job_time_min} means it was clamped to "
        f"--time-min, and any other value means a different code path trimmed "
        f"it than the one this test covers"
    )
    return trimmed_limit


def assert_usage_grew(baseline, entity, limit="GrpTRESRunMins"):
    """Assert that a job starting pushed usage above the recorded baseline."""

    current = used_run_mins(entity, limit)
    assert all(current.get(tres, 0) > used for tres, used in baseline.items()), (
        f"{limit} usage of the {entity} should have grown once the "
        f"backfilled job started, but was {baseline} before and {current} after"
    )


def assert_usage_restored(entity, baseline, limit="GrpTRESRunMins"):
    """Assert usage settles back to exactly the surviving job's baseline.

    The counters floor at zero, so cancelling every job and asserting zero
    cannot tell a correct give-back from one that removed more than was ever
    booked. Measured against a job that is still running, over-removal shows up
    as that job's own usage going missing.
    """

    usage = None
    for _ in atf.timer(timeout=90, poll_interval=5):
        usage = used_run_mins(entity, limit)
        if usage == baseline:
            break

    assert usage == baseline, (
        f"{limit} usage of the {entity} should be back to the {baseline} "
        f"still booked by the job that is left running, but it is {usage}. More "
        f"than {baseline} means usage was leaked, less means the give-back "
        f"removed usage that belongs to the surviving job"
    )


@pytest.mark.parametrize(
    "slurm_priority_type",
    ["priority/multifactor", "priority/basic"],
    indirect=True,
)
def test_backfill_time_min_run_mins_usage(slurm_priority_type):
    """Verify that trimming a --time-min job's time limit does not leak usage.

    Backfill raises a --time-min job's time limit back up to the requested limit
    when it starts it, then trims it to whatever fits before the next reserved
    job. The usage booked against the untrimmed limit has to be given back,
    otherwise it accumulates for jobs that are long gone and blocks the user.

    Here backfill's own node_space map does the trimming. The test below covers
    an advance reservation doing it instead.

    Run under both priority plugins. GrpTRESRunMins is documented without a
    plugin caveat (unlike GrpTRESMins), and priority/basic gives the usage back
    itself in priority_p_job_end(). The jobs run under normal as their own QOS
    while part1 carries qos1, so this also checks that basic gives the
    partition QOS usage back, not only the job QOS.
    """

    node1, node2 = get_test_nodes()

    submit_blocker(node1, blocker_time_limit)
    reserving_job_id = submit_reserving_job()

    # The blocker is the only job booking anything at this point, and it outlives
    # the victim, so its usage is the baseline the give-back is measured against.
    baseline_qos = used_run_mins("qos")
    baseline_assoc = used_run_mins("assoc")
    assert (
        baseline_qos
    ), f"QOS {qos1} should show TRESRunMins usage while the blocker is running"
    assert baseline_assoc, (
        f"Association of {test_user} should show TRESRunMins usage while the "
        f"blocker is running"
    )

    victim_job_id = submit_victim(node2)

    assert atf.get_job_parameter(reserving_job_id, "JobState") == "PENDING", (
        "Reserving job should still be pending, so that it holds the "
        "allocation the backfilled job is trimmed against"
    )

    assert_trimmed_by(victim_job_id, blocker_time_limit, "Backfill's node_space map")

    assert_usage_grew(baseline_qos, "qos")
    assert_usage_grew(baseline_assoc, "assoc")

    atf.cancel_jobs([victim_job_id, reserving_job_id], fatal=True)

    assert_usage_restored("qos", baseline_qos)
    assert_usage_restored("assoc", baseline_assoc)

    atf.cancel_all_jobs(fatal=True)
    assert_usage_restored("qos", {})
    assert_usage_restored("assoc", {})


def test_time_min_blocks_start_when_window_is_shorter():
    """Verify a job does not start when --time-min exceeds the free window.

    The give-back arithmetic is a function of the trimmed limit. If backfill
    ignored --time-min and started the job in a window shorter than that
    floor, the booked-vs-given-back difference would be wrong and none of the
    usage tests above would see it, because they only look at jobs that ran.
    """

    floor_time_min = 40

    node1, node2 = get_test_nodes()
    submit_blocker(node1, blocker_time_limit)
    submit_reserving_job()

    job_id = atf.submit_job_sbatch(
        f"-w {node2} --exclusive -t{job_time_limit} --time-min={floor_time_min} "
        f"-A {account} --wrap 'sleep infinity'",
        fatal=True,
    )

    # Several bf_interval passes; the job must not slip through on a later one.
    started = atf.wait_for_job_state(job_id, "RUNNING", timeout=60)
    assert not started, (
        f"A --time-min={floor_time_min} job should stay pending when the free "
        f"window is about {blocker_time_limit} minutes, but it is "
        f"{atf.get_job_parameter(job_id, 'JobState')} with TimeLimit "
        f"{atf.get_job_parameter(job_id, 'TimeLimit')}"
    )
    assert atf.get_job_parameter(job_id, "JobState") == "PENDING", (
        f"The job should remain PENDING, not "
        f"{atf.get_job_parameter(job_id, 'JobState')}"
    )


def test_backfill_time_min_max_run_mins_pu_usage():
    """Verify trimming does not leak the per-user MaxTRESRunMins counter.

    MaxTRESRunMinsPU is tracked separately from GrpTRESRunMins, on a per-user
    record of the QOS rather than on the QOS as a whole, and is the counter
    ticket 25339 reports as stuck.
    """

    node1, node2 = get_test_nodes()

    atf.run_command(
        f"sacctmgr -i modify qos {qos1} set MaxTRESRunMinsPU=node={run_mins_limit}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    submit_blocker(node1, blocker_time_limit)
    reserving_job_id = submit_reserving_job()

    baseline = used_run_mins("qos", "MaxTRESRunMinsPU")
    assert baseline, (
        f"QOS {qos1} should show MaxTRESRunMinsPU usage for {test_user} while "
        f"the blocker is running"
    )

    victim_job_id = submit_victim(node2)
    assert_trimmed_by(victim_job_id, blocker_time_limit, "Backfill's node_space map")
    assert_usage_grew(baseline, "qos", "MaxTRESRunMinsPU")

    atf.cancel_jobs([victim_job_id, reserving_job_id], fatal=True)
    assert_usage_restored("qos", baseline, "MaxTRESRunMinsPU")

    atf.cancel_all_jobs(fatal=True)
    assert_usage_restored("qos", {}, "MaxTRESRunMinsPU")


def test_reservation_time_min_run_mins_usage():
    """Verify a reservation trimming a --time-min job does not leak usage.

    Backfill lowers a job's time limit so that it does not run into an advance
    reservation. That happens after backfill has already booked usage against
    the job's full time limit, so the difference has to be given back.
    """

    node1, node2 = get_test_nodes()

    submit_blocker(node1, resv_blocker_time_limit)

    # Created only once the blocker is running, since a future reservation would
    # otherwise keep the blocker from starting at all.
    atf.run_command(
        f"scontrol create reservation reservationname={resv1} "
        f"starttime=now+{resv_start_minutes}minutes "
        f"duration={resv_duration_minutes} "
        f"user=root nodes={node2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    reserving_job_id = submit_reserving_job()

    baseline_qos = used_run_mins("qos")
    baseline_assoc = used_run_mins("assoc")
    assert (
        baseline_qos
    ), f"QOS {qos1} should show TRESRunMins usage while the blocker is running"
    assert baseline_assoc, (
        f"Association of {test_user} should show TRESRunMins usage while the "
        f"blocker is running"
    )

    victim_job_id = submit_victim(node2)

    assert atf.get_job_parameter(reserving_job_id, "JobState") == "PENDING", (
        "Reserving job should still be pending, so that the reservation is "
        "what the backfilled job ends up being trimmed against"
    )

    assert_trimmed_by(victim_job_id, resv_start_minutes, "The reservation")

    assert_usage_grew(baseline_qos, "qos")
    assert_usage_grew(baseline_assoc, "assoc")

    atf.cancel_jobs([victim_job_id, reserving_job_id], fatal=True)

    assert_usage_restored("qos", baseline_qos)
    assert_usage_restored("assoc", baseline_assoc)

    atf.cancel_all_jobs(fatal=True)
    assert_usage_restored("qos", {})
    assert_usage_restored("assoc", {})


def test_run_mins_limit_enforced_after_trim():
    """Verify leaked TRESRunMins usage does not go on blocking later jobs.

    The tests above read slurmctld's usage counters. This one checks the
    documented behavior those counters drive: usage that is never given back
    keeps counting against GrpTRESRunMins and holds later jobs on
    QOSGrpCPURunMinutesLimit long after the job that booked it is gone.
    """

    node1, node2 = get_test_nodes()

    atf.run_command(
        f"sacctmgr -i modify qos {qos1} set GrpTRESRunMins=cpu={run_mins_limit}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # The update reaches slurmctld asynchronously through slurmdbd.
    landed = False
    for _ in atf.timer(timeout=60, poll_interval=2):
        output = atf.run_command_output(
            f"scontrol show assoc_mgr qos={qos1} flags=qos",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        if f"GrpTRESRunMins=cpu={run_mins_limit}" in output:
            landed = True
            break
    assert landed, "slurmctld should have picked up the GrpTRESRunMins limit"

    # Positive control, so that the check at the end cannot pass whether or not
    # the limit is enforced at all.
    oversized_job_id = atf.submit_job_sbatch(
        f"-N1 --exclusive -t{oversized_probe_time_limit} -A {account} "
        f"--wrap 'sleep infinity'",
        fatal=True,
    )
    reason = None
    for _ in atf.timer(timeout=60):
        reason = atf.get_job_parameter(oversized_job_id, "Reason")
        if reason == "QOSGrpCPURunMinutesLimit":
            break
    assert reason == "QOSGrpCPURunMinutesLimit", (
        f"A job asking for more than GrpTRESRunMins=cpu={run_mins_limit} should "
        f"be held on QOSGrpCPURunMinutesLimit, but it is held on {reason}"
    )
    atf.cancel_jobs([oversized_job_id], fatal=True)

    # The same leak cycle as the first test.
    submit_blocker(node1, blocker_time_limit)
    submit_reserving_job()
    victim_job_id = submit_victim(node2)
    assert_trimmed_by(victim_job_id, blocker_time_limit, "Backfill's node_space map")

    atf.cancel_all_jobs(fatal=True)

    # Deliberately not asserted on: this test has to fail on the behavior below,
    # not on the counters the tests above already cover.
    for _ in atf.timer(timeout=90, poll_interval=5):
        if not used_run_mins("qos"):
            break

    probe_job_id = atf.submit_job_sbatch(
        f"-N1 --exclusive -t{probe_time_limit} -A {account} "
        f"--wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(probe_job_id, "RUNNING", timeout=60), (
        f"A job fitting under GrpTRESRunMins=cpu={run_mins_limit} should run "
        f"once every other job is gone, but it is held on "
        f"{atf.get_job_parameter(probe_job_id, 'Reason')} with "
        f"{used_run_mins('qos')} still booked against the QOS"
    )


def test_assoc_run_mins_limit_enforced_after_trim():
    """Verify leaked usage does not block later jobs through the association.

    The association keeps its own GrpTRESRunMins counter, separate from the
    QOS one, and holds jobs on AssocGrpCPURunMinutesLimit rather than on
    QOSGrpCPURunMinutesLimit.
    """

    node1, node2 = get_test_nodes()

    atf.run_command(
        f"sacctmgr -i modify user {test_user} where account={account} "
        f"set GrpTRESRunMins=cpu={run_mins_limit}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    landed = False
    for _ in atf.timer(timeout=60, poll_interval=2):
        output = atf.run_command_output(
            f"scontrol show assoc_mgr flags=assoc user={test_user}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        if f"GrpTRESRunMins=cpu={run_mins_limit}" in output:
            landed = True
            break
    assert (
        landed
    ), "slurmctld should have picked up the association GrpTRESRunMins limit"

    oversized_job_id = atf.submit_job_sbatch(
        f"-N1 --exclusive -t{oversized_probe_time_limit} -A {account} "
        f"--wrap 'sleep infinity'",
        fatal=True,
    )
    reason = None
    for _ in atf.timer(timeout=60):
        reason = atf.get_job_parameter(oversized_job_id, "Reason")
        if reason == "AssocGrpCPURunMinutesLimit":
            break
    assert reason == "AssocGrpCPURunMinutesLimit", (
        f"A job asking for more than the association "
        f"GrpTRESRunMins=cpu={run_mins_limit} should be held on "
        f"AssocGrpCPURunMinutesLimit, but it is held on {reason}"
    )
    atf.cancel_jobs([oversized_job_id], fatal=True)

    submit_blocker(node1, blocker_time_limit)
    submit_reserving_job()
    victim_job_id = submit_victim(node2)
    assert_trimmed_by(victim_job_id, blocker_time_limit, "Backfill's node_space map")

    atf.cancel_all_jobs(fatal=True)

    for _ in atf.timer(timeout=90, poll_interval=5):
        if not used_run_mins("assoc"):
            break

    probe_job_id = atf.submit_job_sbatch(
        f"-N1 --exclusive -t{probe_time_limit} -A {account} "
        f"--wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(probe_job_id, "RUNNING", timeout=60), (
        f"A job fitting under the association GrpTRESRunMins=cpu="
        f"{run_mins_limit} should run once every other job is gone, but it is "
        f"held on {atf.get_job_parameter(probe_job_id, 'Reason')} with "
        f"{used_run_mins('assoc')} still booked against the association"
    )
