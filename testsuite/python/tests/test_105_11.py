############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
"""
Cause-aware batch job requeue limits (MaxNodeFailRequeue, MaxPreemptRequeue).

Verifies that node-failure requeues, preemption requeues, and launch-failure
requeues are counted independently, each against its own configurable limit.

Tested:
- Node-failure requeues are held at MaxNodeFailRequeue with
  JobHeldMaxNodeFailRequeue reason.
- Preemption requeues are held at MaxPreemptRequeue with
  JobHeldMaxPreemptRequeue reason.
- Node-failure requeues do NOT count toward MaxBatchRequeue.
- Node-failure requeues do NOT count toward MaxPreemptRequeue.
- Preemption requeues do NOT count toward MaxNodeFailRequeue.
- scontrol requeuehold does NOT consume the preemption budget.
- User-initiated scontrol requeue does NOT consume the preemption budget.
- Operator-initiated scontrol requeue consumes the preemption budget.
- scontrol release resets per-cause counters (fresh allowance).
- MaxPreemptRequeue=0 (the default) means unlimited; a job is never held for
  preemption unless an admin sets a positive limit.
"""
import atf
import pytest

NODE_FAIL_LIMIT = 2
PREEMPT_LIMIT = 2
QOS_LOW = "test_requeue_low"
QOS_HIGH = "test_requeue_high"

pytestmark = pytest.mark.slow


def _assert_requeue_hold(job_id, reason_markers, context):
    job_state = atf.get_job_parameter(job_id, "JobState")
    reason = atf.get_job_parameter(job_id, "Reason") or ""
    priority = atf.get_job_parameter(job_id, "Priority")

    assert job_state == "REQUEUE_HOLD", (
        f"{context}: expected JobState=REQUEUE_HOLD, got {job_state}"
    )
    assert any(marker in reason for marker in reason_markers) or any(
        marker.lower() in reason.lower() for marker in reason_markers
    ), f"{context}: unexpected hold reason {reason}"
    assert str(priority) == "0", (
        f"{context}: held job should have Priority=0, got {priority}"
    )

    return reason


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    atf.require_config_parameter_includes(
        "AccountingStorageEnforce", "associations"
    )
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("PreemptType", "preempt/qos")
    atf.require_config_parameter("PreemptMode", "REQUEUE")
    atf.require_config_parameter("MaxNodeFailRequeue", str(NODE_FAIL_LIMIT))
    # MaxPreemptRequeue defaults to 0 (unlimited); pin a small positive limit
    # here so the hold-at-limit tests can exercise the preemption path.
    atf.require_config_parameter("MaxPreemptRequeue", str(PREEMPT_LIMIT))
    atf.require_config_parameter("MaxBatchRequeue", "5")
    atf.require_config_parameter_includes(
        "SchedulerParameters", "requeue_delay=0"
    )
    atf.require_config_parameter("ReturnToService", "1")
    atf.require_slurm_running()

    cluster = atf.get_config_parameter("ClusterName")
    user = atf.properties["test-user"]
    su = atf.properties["slurm-user"]
    qos_list = f"normal,{QOS_LOW},{QOS_HIGH}"

    atf.run_command(
        f"sacctmgr -i add qos {QOS_LOW} 2>/dev/null || true",
        user=su,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {QOS_HIGH} Preempt={QOS_LOW} "
        f"2>/dev/null || sacctmgr -i modify qos {QOS_HIGH} "
        f"set Preempt={QOS_LOW}",
        user=su,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user} cluster={cluster} account=root "
        f"qos={qos_list} 2>/dev/null || sacctmgr -i modify user {user} "
        f"where cluster={cluster} set qos={qos_list}",
        user=su,
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i modify user {user} where cluster={cluster} "
        f"set qos=normal",
        user=su,
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove qos {QOS_HIGH},{QOS_LOW}",
        user=su,
        quiet=True,
    )


@pytest.fixture(scope="module")
def nodes():
    node_list = list(atf.nodes)
    assert len(node_list) >= 2, "Need at least 2 nodes"
    return node_list[0], node_list[1]


@pytest.fixture(scope="function", autouse=True)
def cleanup_nodes(nodes):
    """Resume any downed nodes after each test."""
    yield
    for node in nodes:
        atf.run_command(
            f"scontrol update nodename={node} state=RESUME",
            user=atf.properties["slurm-user"],
            quiet=True,
        )
        atf.wait_for_node_state(node, "IDLE", timeout=30, fatal=False)


def test_node_fail_hold_at_limit(nodes):
    """A job requeued by node failure is held after MaxNodeFailRequeue."""
    target_node, other_node = nodes

    # Pin a filler job on the other node so our test job only runs on target
    filler = atf.submit_job_sbatch(
        f"-N1 -w {other_node} --exclusive --requeue --wrap 'sleep 600'",
        fatal=True,
    )
    atf.wait_for_job_state(filler, "RUNNING", fatal=True)

    # Submit test job pinned to target node
    job_id = atf.submit_job_sbatch(
        f"-N1 -w {target_node} --exclusive --requeue --wrap 'sleep 600'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Cycle through node failures up to and past the limit
    for cycle in range(1, NODE_FAIL_LIMIT + 2):
        atf.run_command(
            f"scontrol update nodename={target_node} state=DOWN reason=test_cycle_{cycle}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        # Wait for job to leave RUNNING
        atf.repeat_until(
            lambda: atf.get_job_parameter(job_id, "JobState"),
            lambda s: s != "RUNNING",
            timeout=30,
            fatal=True,
        )

        if cycle <= NODE_FAIL_LIMIT:
            # Should NOT be held yet
            reason = atf.get_job_parameter(job_id, "Reason") or ""
            assert "MaxNodeFailRequeue" not in reason, (
                f"Job should not be held after {cycle} node failures "
                f"(limit is {NODE_FAIL_LIMIT}), Reason={reason}"
            )
            # Resume node for next cycle
            atf.run_command(
                f"scontrol update nodename={target_node} state=RESUME",
                user=atf.properties["slurm-user"],
                fatal=True,
            )
            atf.wait_for_node_state(target_node, "IDLE", fatal=True)
            atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
        else:
            # Past the limit - job must be held
            _assert_requeue_hold(
                job_id,
                ["node_failure_requeue_limit", "JobHeldMaxNodeFailRequeue"],
                f"Job after {cycle} node failures",
            )

    atf.cancel_jobs([filler, job_id])


def test_node_fail_does_not_trip_other_requeue_limits(nodes):
    """Node-failure requeues must NOT count toward other requeue limits."""
    target_node, other_node = nodes
    filler = None
    job_id = None
    max_batch_requeue = 1
    max_preempt_requeue = 1
    max_node_fail_requeue = max_batch_requeue + 2
    node_fail_cycles = max_batch_requeue + 1

    try:
        atf.set_config_parameter("MaxBatchRequeue", str(max_batch_requeue))
        atf.set_config_parameter("MaxPreemptRequeue", str(max_preempt_requeue))
        atf.set_config_parameter(
            "MaxNodeFailRequeue", str(max_node_fail_requeue)
        )
        atf.run_command(
            "scontrol reconfigure",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

        filler = atf.submit_job_sbatch(
            f"-N1 -w {other_node} --exclusive --requeue --wrap 'sleep 600'",
            fatal=True,
        )
        atf.wait_for_job_state(filler, "RUNNING", fatal=True)

        job_id = atf.submit_job_sbatch(
            f"-N1 -w {target_node} --exclusive --requeue --wrap 'sleep 600'",
            fatal=True,
        )
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

        for cycle in range(1, node_fail_cycles + 1):
            atf.run_command(
                f"scontrol update nodename={target_node} state=DOWN "
                f"reason=batch_independent_{cycle}",
                user=atf.properties["slurm-user"],
                fatal=True,
            )
            atf.repeat_until(
                lambda: atf.get_job_parameter(job_id, "JobState"),
                lambda s: s != "RUNNING",
                timeout=30,
                fatal=True,
            )

            reason = atf.get_job_parameter(job_id, "Reason") or ""
            assert "MaxRequeue" not in reason, (
                f"Node-failure requeue should not trigger MaxBatchRequeue "
                f"after {cycle} node failures, Reason={reason}"
            )
            assert "MaxPreemptRequeue" not in reason, (
                f"Node-failure requeue should not trigger "
                f"MaxPreemptRequeue after {cycle} node failures, "
                f"Reason={reason}"
            )
            assert "preemption_requeue_limit" not in reason.lower(), (
                f"Node-failure requeue should not trigger preemption hold "
                f"after {cycle} node failures, Reason={reason}"
            )
            assert "MaxNodeFail" not in reason, (
                f"Node-failure requeue should not hit MaxNodeFailRequeue "
                f"before {max_node_fail_requeue + 1} failures, "
                f"Reason={reason}"
            )

            atf.run_command(
                f"scontrol update nodename={target_node} state=RESUME",
                user=atf.properties["slurm-user"],
                fatal=True,
            )
            atf.wait_for_node_state(target_node, "IDLE", fatal=True)
            atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    finally:
        job_ids = [job for job in [filler, job_id] if job]
        if job_ids:
            atf.cancel_jobs(job_ids)

        atf.set_config_parameter("MaxBatchRequeue", "5")
        atf.set_config_parameter("MaxNodeFailRequeue", str(NODE_FAIL_LIMIT))
        atf.set_config_parameter("MaxPreemptRequeue", str(PREEMPT_LIMIT))
        atf.run_command(
            "scontrol reconfigure",
            user=atf.properties["slurm-user"],
            fatal=True,
        )


def test_preempt_hold_at_limit(nodes):
    """A job preempted past MaxPreemptRequeue is held."""
    target_node = nodes[0]

    for cycle in range(1, PREEMPT_LIMIT + 2):
        # Submit low-priority victim
        if cycle == 1:
            victim = atf.submit_job_sbatch(
                f"-N1 -w {target_node} --exclusive --requeue "
                f"--qos={QOS_LOW} --wrap 'sleep 600'",
                fatal=True,
            )
        atf.wait_for_job_state(victim, "RUNNING", fatal=True, timeout=60)

        # Submit high-priority preemptor
        preemptor = atf.submit_job_sbatch(
            f"-N1 -w {target_node} --exclusive "
            f"--qos={QOS_HIGH} --wrap 'sleep 15'",
            fatal=True,
        )
        atf.wait_for_job_state(preemptor, "RUNNING", fatal=True)

        # Wait for preemptor to finish
        atf.wait_for_job_state(preemptor, "DONE", fatal=True, timeout=30)

        if cycle <= PREEMPT_LIMIT:
            reason = atf.get_job_parameter(victim, "Reason") or ""
            assert "MaxPreemptRequeue" not in reason, (
                f"Victim should not be held after {cycle} preemptions "
                f"(limit is {PREEMPT_LIMIT}), Reason={reason}"
            )
        else:
            _assert_requeue_hold(
                victim,
                ["preemption_requeue_limit", "JobHeldMaxPreemptRequeue"],
                f"Victim after {cycle} preemptions",
            )

    atf.cancel_jobs([victim])


def test_preempt_does_not_trip_node_fail_requeue_limit(nodes):
    """Preemption requeues must NOT count toward MaxNodeFailRequeue."""
    target_node = nodes[0]
    victim = None

    try:
        # Keep preemption unlimited and make the node-failure limit tiny. If
        # preemption incorrectly consumed the node-failure counter, the victim
        # would be held by the second preemption.
        atf.set_config_parameter("MaxNodeFailRequeue", "1")
        atf.set_config_parameter("MaxPreemptRequeue", "0")
        atf.run_command(
            "scontrol reconfigure",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

        victim = atf.submit_job_sbatch(
            f"-N1 -w {target_node} --exclusive --requeue "
            f"--qos={QOS_LOW} --wrap 'sleep 600'",
            fatal=True,
        )

        for cycle in range(1, 3):
            atf.wait_for_job_state(victim, "RUNNING", fatal=True, timeout=60)
            preemptor = atf.submit_job_sbatch(
                f"-N1 -w {target_node} --exclusive "
                f"--qos={QOS_HIGH} --wrap 'sleep 2'",
                fatal=True,
            )
            atf.repeat_until(
                lambda: atf.get_job_parameter(victim, "JobState"),
                lambda s: s != "RUNNING",
                timeout=30,
                fatal=True,
            )
            atf.wait_for_job_state(preemptor, "DONE", fatal=True, timeout=30)

            reason = atf.get_job_parameter(victim, "Reason") or ""
            job_state = atf.get_job_parameter(victim, "JobState")
            assert "MaxNodeFailRequeue" not in reason, (
                f"Preemption should not trigger MaxNodeFailRequeue after "
                f"{cycle} preemptions, Reason={reason}"
            )
            assert "node_failure_requeue_limit" not in reason.lower(), (
                f"Preemption should not trigger node-failure hold after "
                f"{cycle} preemptions, Reason={reason}"
            )
            assert job_state != "REQUEUE_HOLD", (
                f"Victim should not be held by node-failure limit after "
                f"{cycle} preemptions, state={job_state}, Reason={reason}"
            )
    finally:
        if victim:
            atf.cancel_jobs([victim])

        atf.set_config_parameter("MaxNodeFailRequeue", str(NODE_FAIL_LIMIT))
        atf.set_config_parameter("MaxPreemptRequeue", str(PREEMPT_LIMIT))
        atf.run_command(
            "scontrol reconfigure",
            user=atf.properties["slurm-user"],
            fatal=True,
        )


def test_requeuehold_exempt_from_counting():
    """scontrol requeuehold must NOT count toward MaxPreemptRequeue."""
    job_id = atf.submit_job_sbatch(
        "-N1 --exclusive --requeue --wrap 'sleep 600'",
        fatal=True,
    )
    try:
        for cycle in range(1, PREEMPT_LIMIT + 2):
            atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)

            # requeuehold - should NOT count
            atf.run_command(
                f"scontrol requeuehold {job_id}",
                user=atf.properties["slurm-user"],
                fatal=True,
            )
            atf.repeat_until(
                lambda: (
                    atf.get_job_parameter(job_id, "Reason") or "",
                    atf.get_job_parameter(job_id, "Priority"),
                    atf.get_job_parameter(job_id, "JobState"),
                ),
                lambda result: (
                    "job_requeued_in_held_state" in result[0].lower()
                    or "JobHeldUser" in result[0]
                    or "JobHeldAdmin" in result[0]
                )
                and str(result[1]) == "0"
                and result[2] in ("PENDING", "REQUEUE_HOLD"),
                timeout=30,
                fatal=True,
            )
            job_state = atf.get_job_parameter(job_id, "JobState")
            reason = atf.get_job_parameter(job_id, "Reason") or ""
            priority = atf.get_job_parameter(job_id, "Priority")
            assert job_state in ("PENDING", "REQUEUE_HOLD"), (
                f"requeuehold should leave job held pending, got {job_state}"
            )
            assert (
                "job_requeued_in_held_state" in reason.lower()
                or "JobHeldUser" in reason
                or "JobHeldAdmin" in reason
            ), (
                f"requeuehold should keep a normal hold reason after "
                f"{cycle} attempts, got {reason}"
            )
            assert "MaxPreemptRequeue" not in reason, (
                f"requeuehold should NOT trigger preemption limit after "
                f"{cycle} attempts, Reason={reason}"
            )
            assert "preemption_requeue_limit" not in reason.lower(), (
                f"requeuehold should NOT relabel the hold as preemption "
                f"after {cycle} attempts, Reason={reason}"
            )
            assert str(priority) == "0", (
                f"requeuehold job should have Priority=0, got {priority}"
            )

            if cycle <= PREEMPT_LIMIT:
                atf.run_command(
                    f"scontrol release {job_id}",
                    user=atf.properties["slurm-user"],
                    fatal=True,
                )
    finally:
        atf.cancel_jobs([job_id])


def test_user_requeue_exempt_from_counting():
    """User-initiated scontrol requeue must NOT count toward MaxPreemptRequeue."""
    job_id = atf.submit_job_sbatch(
        "-N1 --exclusive --requeue --wrap 'sleep 600'",
        fatal=True,
    )

    try:
        for cycle in range(PREEMPT_LIMIT + 2):
            atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)
            atf.run_command(
                f"scontrol requeue {job_id}",
                user=atf.properties["test-user"],
                fatal=True,
            )
            reason = atf.get_job_parameter(job_id, "Reason") or ""
            assert (
                "MaxPreemptRequeue" not in reason
                and "preemption_requeue_limit" not in reason.lower()
            ), (
                f"user requeue should not trigger preemption limit after "
                f"{cycle + 1} requeues, Reason={reason}"
            )
    finally:
        atf.cancel_jobs([job_id])


def test_operator_requeue_counts_toward_preempt_limit():
    """Operator-initiated scontrol requeue counts toward MaxPreemptRequeue."""
    job_id = atf.submit_job_sbatch(
        "-N1 --exclusive --requeue --wrap 'sleep 600'",
        fatal=True,
    )

    try:
        for cycle in range(1, PREEMPT_LIMIT + 2):
            atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)
            atf.run_command(
                f"scontrol requeue {job_id}",
                user=atf.properties["slurm-user"],
                fatal=True,
            )
            reason = atf.get_job_parameter(job_id, "Reason") or ""

            if cycle <= PREEMPT_LIMIT:
                assert "MaxPreemptRequeue" not in reason, (
                    f"operator requeue should not hold before limit "
                    f"{PREEMPT_LIMIT}, cycle={cycle}, Reason={reason}"
                )
            else:
                atf.wait_for_job_state(
                    job_id, "REQUEUE_HOLD", fatal=True, timeout=30
                )
                _assert_requeue_hold(
                    job_id,
                    [
                        "preemption_requeue_limit",
                        "JobHeldMaxPreemptRequeue",
                    ],
                    f"operator requeue after {cycle} requeues",
                )
    finally:
        atf.cancel_jobs([job_id])


def test_release_resets_counters(nodes):
    """scontrol release must reset per-cause counters."""
    target_node, other_node = nodes

    filler = atf.submit_job_sbatch(
        f"-N1 -w {other_node} --exclusive --requeue --wrap 'sleep 600'",
        fatal=True,
    )
    atf.wait_for_job_state(filler, "RUNNING", fatal=True)

    job_id = atf.submit_job_sbatch(
        f"-N1 -w {target_node} --exclusive --requeue --wrap 'sleep 600'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Hit the node-fail limit
    for cycle in range(NODE_FAIL_LIMIT + 1):
        atf.run_command(
            f"scontrol update nodename={target_node} state=DOWN reason=test_{cycle}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        atf.repeat_until(
            lambda: atf.get_job_parameter(job_id, "JobState"),
            lambda s: s != "RUNNING",
            timeout=30,
            fatal=True,
        )
        if cycle < NODE_FAIL_LIMIT:
            atf.run_command(
                f"scontrol update nodename={target_node} state=RESUME",
                user=atf.properties["slurm-user"],
                fatal=True,
            )
            atf.wait_for_node_state(target_node, "IDLE", fatal=True)
            atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Job should be held now
    _assert_requeue_hold(
        job_id,
        ["node_failure_requeue_limit", "JobHeldMaxNodeFailRequeue"],
        "job at node-failure limit before release",
    )

    # Release the hold - counters should reset
    atf.run_command(
        f"scontrol update nodename={target_node} state=RESUME",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_node_state(target_node, "IDLE", fatal=True)
    atf.run_command(
        f"scontrol release {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)

    # One more node failure should NOT immediately re-hold
    # (counter was reset by release)
    atf.run_command(
        f"scontrol update nodename={target_node} state=DOWN reason=after_release",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_job_parameter(job_id, "JobState"),
        lambda s: s != "RUNNING",
        timeout=30,
        fatal=True,
    )
    reason = atf.get_job_parameter(job_id, "Reason") or ""
    assert "MaxNodeFail" not in reason, (
        f"After release, one node failure should NOT re-hold "
        f"(counters should be reset), Reason={reason}"
    )

    atf.cancel_jobs([filler, job_id])


def test_unlimited_preempt_requeue(nodes):
    """MaxPreemptRequeue=0 (the default) means unlimited - never held for preemption."""
    target_node = nodes[0]
    victim = None

    try:
        # 0 is the built-in default for MaxPreemptRequeue (see read_config.h);
        # set it explicitly because the module fixture pins a positive limit.
        atf.set_config_parameter("MaxPreemptRequeue", "0")
        atf.run_command(
            "scontrol reconfigure",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        assert str(atf.get_config_parameter("MaxPreemptRequeue")) == "0", (
            "MaxPreemptRequeue should read 0 (unlimited) after being configured"
        )

        victim = atf.submit_job_sbatch(
            f"-N1 -w {target_node} --exclusive --requeue "
            f"--qos={QOS_LOW} --wrap 'sleep 600'",
            fatal=True,
        )

        # Preempt more times than the original limit
        for cycle in range(PREEMPT_LIMIT + 2):
            atf.wait_for_job_state(victim, "RUNNING", fatal=True, timeout=60)
            preemptor = atf.submit_job_sbatch(
                f"-N1 -w {target_node} --exclusive "
                f"--qos={QOS_HIGH} --wrap 'sleep 2'",
                fatal=True,
            )
            atf.wait_for_job_state(preemptor, "DONE", fatal=True, timeout=30)

        # Victim should still be PENDING (not held)
        reason = atf.get_job_parameter(victim, "Reason") or ""
        priority = atf.get_job_parameter(victim, "Priority")
        assert "MaxPreemptRequeue" not in reason, (
            f"With MaxPreemptRequeue=0, job should never be held for "
            f"preemption, got Reason={reason}"
        )
        assert str(priority) != "0", (
            f"Job should not have Priority=0 with unlimited preemption"
        )
    finally:
        if victim:
            atf.cancel_jobs([victim])

        # Restore original limit
        atf.set_config_parameter("MaxPreemptRequeue", str(PREEMPT_LIMIT))
        atf.run_command(
            "scontrol reconfigure",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
