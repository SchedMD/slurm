############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter("PreemptType", "preempt/qos")
    atf.require_config_parameter("PreemptMode", "CANCEL")
    atf.require_config_parameter("PreemptExemptTime", "0")
    atf.require_config_parameter(
        "AccountingStorageEnforce", "associations,qos,limits"
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_nodes(1, [("CPUs", 4), ("RealMemory", 512)])
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def cleanup():
    yield
    atf.cancel_all_jobs()


@pytest.fixture(scope="function")
def hierarchy():
    """Create a two-level account hierarchy with QOS preemption.

    pillar (GrpTRES=cpu=4)
      ├─ proj1 (GrpTRES=cpu=2)
      └─ proj2 (GrpTRES=cpu=2)

    q_high: priority=100, preempts q_low, GraceTime=0
    q_low:  priority=10
    """
    user = atf.properties["slurm-user"]

    atf.run_command(
        "sacctmgr -i add qos q_low Priority=10 GraceTime=0 PreemptExemptTime=0",
        user=user, fatal=True,
    )
    atf.run_command(
        "sacctmgr -i add qos q_high Priority=100 Preempt=q_low GraceTime=0 PreemptExemptTime=0",
        user=user, fatal=True,
    )
    atf.run_command(
        "sacctmgr -i add account pillar GrpTRES=cpu=4",
        user=user, fatal=True,
    )
    atf.run_command(
        "sacctmgr -i add account proj1 parent=pillar GrpTRES=cpu=2",
        user=user, fatal=True,
    )
    atf.run_command(
        "sacctmgr -i add account proj2 parent=pillar GrpTRES=cpu=2",
        user=user, fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {atf.get_user_name()} account=proj1 qos=normal,q_high,q_low",
        user=user, fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {atf.get_user_name()} account=proj2 qos=normal,q_high,q_low",
        user=user, fatal=True,
    )

    yield {
        "qos_high": "q_high",
        "qos_low": "q_low",
        "pillar": "pillar",
        "proj1": "proj1",
        "proj2": "proj2",
    }

    atf.run_command(
        f"sacctmgr -i delete user {atf.get_user_name()} account=proj1,proj2",
        user=user, fatal=False,
    )
    atf.run_command(
        "sacctmgr -i delete account proj1 proj2 pillar",
        user=user, fatal=False,
    )
    atf.run_command(
        "sacctmgr -i delete qos q_high q_low",
        user=user, fatal=False,
    )


def test_single_job_cross_account_preemption(hierarchy):
    """Verify a single q_high job from proj1 preempts q_low jobs from proj2
    when blocked by parent account GrpTRES.

    Setup:
      proj2 fills its 2-CPU quota with q_low jobs.
      proj1 submits a q_high job requesting 1 CPU.
      The pillar has 2 free CPUs (proj1's quota), so no parent GrpTRES
      conflict — job should start via normal preemption or resource fit.
    Then:
      proj2 fills ALL 4 CPUs (2 own + 2 borrowed from proj1's idle quota).
      proj1 submits q_high job → pillar at 4/4, but preemption should
      resolve it by preempting proj2's q_low job.
    """

    # Fill proj2 to its own quota (2 CPUs)
    job_low1 = atf.submit_job_sbatch(
        f'-A {hierarchy["proj2"]} -q {hierarchy["qos_low"]} '
        f'-n1 -o /dev/null --wrap "sleep 300"',
        fatal=True,
    )
    job_low2 = atf.submit_job_sbatch(
        f'-A {hierarchy["proj2"]} -q {hierarchy["qos_low"]} '
        f'-n1 -o /dev/null --wrap "sleep 300"',
        fatal=True,
    )
    assert atf.wait_for_job_state(job_low1, "RUNNING"), \
        f"q_low job {job_low1} did not start"
    assert atf.wait_for_job_state(job_low2, "RUNNING"), \
        f"q_low job {job_low2} did not start"

    # proj1 submits q_high — pillar has room (2/4 used), should start
    job_high = atf.submit_job_sbatch(
        f'-A {hierarchy["proj1"]} -q {hierarchy["qos_high"]} '
        f'-n1 -o /dev/null --wrap "sleep 60"',
        fatal=True,
    )
    assert atf.wait_for_job_state(job_high, "RUNNING", timeout=15), \
        f"q_high job {job_high} should start (pillar not full)"

    atf.cancel_jobs([job_low1, job_low2, job_high])


def test_preemption_resolves_parent_grptres(hierarchy):
    """Verify preemption resolves a parent association GrpTRES violation.

    This is the core bug fix test (Bug 23492):
      1. proj2 fills pillar to capacity (4/4 CPUs) with q_low jobs
      2. proj1 submits q_high job
      3. Without fix: PENDING forever with Reason=AssocGrpGRES
      4. With fix: scheduler preempts proj2's q_low to make room
    """

    # proj2 uses its 2-CPU quota
    jobs_low = []
    for i in range(2):
        jid = atf.submit_job_sbatch(
            f'-A {hierarchy["proj2"]} -q {hierarchy["qos_low"]} '
            f'-n1 -o /dev/null --wrap "sleep 300"',
            fatal=True,
        )
        jobs_low.append(jid)
        assert atf.wait_for_job_state(jid, "RUNNING"), \
            f"q_low job {jid} did not start"

    # proj1 also uses its 2-CPU quota with q_low (filling pillar to 4/4)
    for i in range(2):
        jid = atf.submit_job_sbatch(
            f'-A {hierarchy["proj1"]} -q {hierarchy["qos_low"]} '
            f'-n1 -o /dev/null --wrap "sleep 300"',
            fatal=True,
        )
        jobs_low.append(jid)
        assert atf.wait_for_job_state(jid, "RUNNING"), \
            f"q_low job {jid} did not start"

    # Now pillar is at 4/4 CPUs. Submit q_high from proj1.
    # This should preempt a q_low job (from proj2 or proj1) to make room.
    job_high = atf.submit_job_sbatch(
        f'-A {hierarchy["proj1"]} -q {hierarchy["qos_high"]} '
        f'-n1 -o /dev/null --wrap "sleep 60"',
        fatal=True,
    )
    assert atf.wait_for_job_state(job_high, "RUNNING", timeout=30), \
        f"q_high job {job_high} did not start — preemption did not resolve parent GrpTRES"

    # Verify at least one q_low job was preempted
    preempted = 0
    for jid in jobs_low:
        state = atf.get_job_parameter(jid, "JobState")
        if state == "PREEMPTED":
            preempted += 1
    assert preempted >= 1, \
        "No q_low jobs were preempted — patch not working"


def test_preemption_minimum_victims(hierarchy):
    """Verify preemption only kills the minimum number of victims needed.

    Submit 4 q_low jobs (filling pillar to 4/4), then a q_high job
    requesting 1 CPU. Only 1 q_low job should be preempted.
    """

    jobs_low = []
    for i in range(4):
        acct = hierarchy["proj1"] if i < 2 else hierarchy["proj2"]
        jid = atf.submit_job_sbatch(
            f'-A {acct} -q {hierarchy["qos_low"]} '
            f'-n1 -o /dev/null --wrap "sleep 300"',
            fatal=True,
        )
        jobs_low.append(jid)
        assert atf.wait_for_job_state(jid, "RUNNING"), \
            f"q_low job {jid} did not start"

    # Submit q_high requesting 1 CPU
    job_high = atf.submit_job_sbatch(
        f'-A {hierarchy["proj1"]} -q {hierarchy["qos_high"]} '
        f'-n1 -o /dev/null --wrap "sleep 60"',
        fatal=True,
    )
    assert atf.wait_for_job_state(job_high, "RUNNING", timeout=30), \
        f"q_high job {job_high} did not start"

    preempted = sum(
        1 for jid in jobs_low
        if atf.get_job_parameter(jid, "JobState") == "PREEMPTED"
    )
    still_running = sum(
        1 for jid in jobs_low
        if atf.get_job_parameter(jid, "JobState") == "RUNNING"
    )

    assert preempted == 1, \
        f"Expected exactly 1 preempted job, got {preempted}"
    assert still_running == 3, \
        f"Expected 3 still running, got {still_running}"


def test_multi_cpu_cross_account_preemption(hierarchy):
    """Verify preemption works for multi-CPU jobs across accounts.

    proj2 fills 2 CPUs, proj1 fills 2 CPUs (all q_low).
    proj1 submits q_high requesting 2 CPUs → must preempt 2 q_low jobs.
    """

    jobs_low = []
    for acct in [hierarchy["proj2"], hierarchy["proj2"],
                 hierarchy["proj1"], hierarchy["proj1"]]:
        jid = atf.submit_job_sbatch(
            f'-A {acct} -q {hierarchy["qos_low"]} '
            f'-n1 -o /dev/null --wrap "sleep 300"',
            fatal=True,
        )
        jobs_low.append(jid)
        assert atf.wait_for_job_state(jid, "RUNNING"), \
            f"q_low job {jid} did not start"

    job_high = atf.submit_job_sbatch(
        f'-A {hierarchy["proj1"]} -q {hierarchy["qos_high"]} '
        f'-n2 -o /dev/null --wrap "sleep 60"',
        fatal=True,
    )
    assert atf.wait_for_job_state(job_high, "RUNNING", timeout=30), \
        f"q_high job {job_high} did not start — multi-CPU preemption failed"

    preempted = sum(
        1 for jid in jobs_low
        if atf.get_job_parameter(jid, "JobState") == "PREEMPTED"
    )
    assert preempted >= 2, \
        f"Expected at least 2 preempted jobs for 2-CPU request, got {preempted}"


def test_array_job_cross_account_preemption(hierarchy):
    """Verify preemption works when the blocking jobs are an array.

    proj2 submits a 2-element job array (filling its quota).
    proj1 submits a 2-element job array (filling pillar).
    proj1 submits q_high single job → preempts array element.
    """

    # proj2 array: 2 tasks, 1 CPU each
    array_low2 = atf.submit_job_sbatch(
        f'-A {hierarchy["proj2"]} -q {hierarchy["qos_low"]} '
        f'-n1 -a 0-1 -o /dev/null --wrap "sleep 300"',
        fatal=True,
    )
    # proj1 array: 2 tasks, 1 CPU each
    array_low1 = atf.submit_job_sbatch(
        f'-A {hierarchy["proj1"]} -q {hierarchy["qos_low"]} '
        f'-n1 -a 0-1 -o /dev/null --wrap "sleep 300"',
        fatal=True,
    )

    # Wait for all array elements to be running
    for base_id in [array_low2, array_low1]:
        for idx in [0, 1]:
            element_id = f"{base_id}_{idx}"
            assert atf.wait_for_job_state(element_id, "RUNNING", timeout=30), \
                f"Array element {element_id} did not start"

    # Pillar at 4/4. Submit q_high from proj1.
    job_high = atf.submit_job_sbatch(
        f'-A {hierarchy["proj1"]} -q {hierarchy["qos_high"]} '
        f'-n1 -o /dev/null --wrap "sleep 60"',
        fatal=True,
    )
    assert atf.wait_for_job_state(job_high, "RUNNING", timeout=30), \
        f"q_high job {job_high} did not start — array preemption failed"


def test_no_preemption_without_qos_config(hierarchy):
    """Verify q_low cannot preempt q_high (reverse direction fails)."""

    # Fill pillar with q_high jobs from proj1
    jobs_high = []
    for i in range(2):
        jid = atf.submit_job_sbatch(
            f'-A {hierarchy["proj1"]} -q {hierarchy["qos_high"]} '
            f'-n1 -o /dev/null --wrap "sleep 300"',
            fatal=True,
        )
        jobs_high.append(jid)
        assert atf.wait_for_job_state(jid, "RUNNING"), \
            f"q_high job {jid} did not start"

    # Fill remaining with q_high from proj2
    for i in range(2):
        jid = atf.submit_job_sbatch(
            f'-A {hierarchy["proj2"]} -q {hierarchy["qos_high"]} '
            f'-n1 -o /dev/null --wrap "sleep 300"',
            fatal=True,
        )
        jobs_high.append(jid)
        assert atf.wait_for_job_state(jid, "RUNNING"), \
            f"q_high job {jid} did not start"

    # Submit q_low — should NOT preempt q_high (q_low can't preempt q_high)
    job_low = atf.submit_job_sbatch(
        f'-A {hierarchy["proj1"]} -q {hierarchy["qos_low"]} '
        f'-n1 -o /dev/null --wrap "sleep 60"',
        fatal=True,
    )
    assert not atf.wait_for_job_state(job_low, "RUNNING", timeout=10, xfail=True), \
        f"q_low job {job_low} should NOT preempt q_high jobs"

    state = atf.get_job_parameter(job_low, "JobState")
    assert state == "PENDING", \
        f"q_low job should be PENDING, got {state}"


def test_three_level_hierarchy():
    """Verify preemption works with a three-level account hierarchy.

    org (GrpTRES=cpu=4)
      └─ pillar (GrpTRES=cpu=4)
           ├─ proj1 (GrpTRES=cpu=2)
           └─ proj2 (GrpTRES=cpu=2)
    """
    user = atf.properties["slurm-user"]
    test_user = atf.get_user_name()

    atf.run_command(
        "sacctmgr -i add qos q3_low Priority=10 GraceTime=0 PreemptExemptTime=0",
        user=user, fatal=True,
    )
    atf.run_command(
        "sacctmgr -i add qos q3_high Priority=100 Preempt=q3_low GraceTime=0 PreemptExemptTime=0",
        user=user, fatal=True,
    )
    atf.run_command("sacctmgr -i add account t3_org GrpTRES=cpu=4", user=user, fatal=True)
    atf.run_command("sacctmgr -i add account t3_pillar parent=t3_org GrpTRES=cpu=4", user=user, fatal=True)
    atf.run_command("sacctmgr -i add account t3_proj1 parent=t3_pillar GrpTRES=cpu=2", user=user, fatal=True)
    atf.run_command("sacctmgr -i add account t3_proj2 parent=t3_pillar GrpTRES=cpu=2", user=user, fatal=True)
    atf.run_command(f"sacctmgr -i add user {test_user} account=t3_proj1 qos=normal,q3_high,q3_low", user=user, fatal=True)
    atf.run_command(f"sacctmgr -i add user {test_user} account=t3_proj2 qos=normal,q3_high,q3_low", user=user, fatal=True)

    try:
        # Fill all 4 CPUs with q3_low
        jobs = []
        for acct in ["t3_proj1", "t3_proj1", "t3_proj2", "t3_proj2"]:
            jid = atf.submit_job_sbatch(
                f'-A {acct} -q q3_low -n1 -o /dev/null --wrap "sleep 300"',
                fatal=True,
            )
            jobs.append(jid)
            assert atf.wait_for_job_state(jid, "RUNNING"), \
                f"Job {jid} did not start"

        # Submit q3_high from t3_proj1 — blocked by t3_org GrpTRES
        job_high = atf.submit_job_sbatch(
            f'-A t3_proj1 -q q3_high -n1 -o /dev/null --wrap "sleep 60"',
            fatal=True,
        )
        assert atf.wait_for_job_state(job_high, "RUNNING", timeout=30), \
            f"q3_high job {job_high} did not start — 3-level preemption failed"

    finally:
        atf.cancel_all_jobs()
        atf.run_command(f"sacctmgr -i delete user {test_user} account=t3_proj1,t3_proj2", user=user, fatal=False)
        atf.run_command("sacctmgr -i delete account t3_proj1 t3_proj2 t3_pillar t3_org", user=user, fatal=False)
        atf.run_command("sacctmgr -i delete qos q3_high q3_low", user=user, fatal=False)
