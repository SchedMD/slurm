############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
"""
Ticket 24603: Het job with different QoS per component.

Pre-fix (Slurm < 26.05): slurmctld aggregates TRES across all components
that share the same association and validates once using the first component's
partition and QoS. With DenyOnLimit: valid het jobs are rejected at submit.
Without DenyOnLimit: aggregate limit is not enforced at submit or at schedule,
so the job is accepted and often runs to COMPLETED (or may stay PENDING).

Post-fix (Slurm >= 26.05): TRES are aggregated only per (assoc, job QOS,
partition) group. With or without DenyOnLimit, the same het jobs are accepted
and run.

Coverage (grouped by scenario; several tests skip if the case only applies before
or from Slurm 26.05—see skipif reason strings on those tests).

- Job-level --qos=: without DenyOnLimit → accept (pending or completed); post-fix DenyOnLimit
  → accept and run (skipif on Slurm < 26.05); without Deny → run to completion on all versions;
  GPU partition fully busy → het stays PENDING (scheduling).
- Partition DefaultQOS (no job --qos=): no-Deny pending; post-fix Deny → accept and run
  (skipif on older); no-Deny → run to completion on all versions.
- Min/Max gres/r1 per component (CPU max 0, GPU min 4 r1): no-Deny pending; post-fix Deny and
  no-Deny → run to completion (skipif on older where marked).
- Association GrpTRES: one association—2+2 > cap rejected at submit; 1+1 or same-partition
  shapes accepted; two associations—2+2 with split -A accepted; running 1-node or
  het blocker → follow-up het PENDING until capacity frees (GrpTRES=2 and GrpTRES=3
  cases; Slurm >= 26.05 where skipif); partition DefaultQOS GrpTRES variants.
- Duplicate (assoc, job QOS, partition) on both het lines: stack within MaxTresPerUser
  → accept or reject at submit.
- Combined-limit (stacked TRES in one bucket): QoS limit_factor × GrpTRES; shared
  (assoc, job QOS); shared (assoc, partition).

Requires: AccountingStorageEnforce=limits, accounting,
AccountingStorageTRES including gres/r1, SelectType=select/cons_tres,
SelectTypeParameters=CR_CPU, GresTypes=r1, two partitions, 4 nodes with
Gres=r1:2 each (2 per partition).
"""
import os
import time

import atf
import pytest

test_name = os.path.splitext(os.path.basename(__file__))[0]
# Partitions: CPU (2 nodes), GPU (2 nodes)
p_cpu = f"{test_name}_p_cpu"
p_gpu = f"{test_name}_p_gpu"
# QoS with DenyOnLimit: CPU limit 2 nodes, GPU limit 10 (so 2 is within limit)
qos_cpu_deny = f"{test_name}_qos_cpu_deny"
qos_gpu_deny = f"{test_name}_qos_gpu_deny"
# QoS without DenyOnLimit (same limits)
qos_cpu_nodeny = f"{test_name}_qos_cpu_nodeny"
qos_gpu_nodeny = f"{test_name}_qos_gpu_nodeny"
# Partition-level QoS (used as partition DefaultQOS; no --qos= in script)
qos_p_cpu_def_deny = f"{test_name}_p_cpu_def_deny"
qos_p_gpu_def_deny = f"{test_name}_p_gpu_def_deny"
qos_p_cpu_def_nodeny = f"{test_name}_p_cpu_def_nodeny"
qos_p_gpu_def_nodeny = f"{test_name}_p_gpu_def_nodeny"
acct = f"{test_name}_acct"
# Min/Max gres/r1 het scenario (job-level QoS per component)
acct_tres_gres = f"{test_name}_acct_tres_gres"
qos_tres_cpu_deny = f"{test_name}_qos_tres_cpu_deny"
qos_tres_gpu_deny = f"{test_name}_qos_tres_gpu_deny"
qos_tres_cpu_nodeny = f"{test_name}_qos_tres_cpu_nodeny"
qos_tres_gpu_nodeny = f"{test_name}_qos_tres_gpu_nodeny"
# Second account for association GrpTRES tests (9–9c)
acct_assoc = f"{test_name}_acct_assoc"
qos_assoc_cpu = f"{test_name}_qos_assoc_cpu"
qos_assoc_gpu = f"{test_name}_qos_assoc_gpu"
# Two accounts: GrpTRES on each (pair to test 9 single-assoc rejection)
acct_assoc2_a = f"{test_name}_acct_assoc2_a"
acct_assoc2_b = f"{test_name}_acct_assoc2_b"
qos_assoc2_cpu = f"{test_name}_qos_assoc2_cpu"
qos_assoc2_gpu = f"{test_name}_qos_assoc2_gpu"
# Combined-limit tests 10–12: separate accounts and QoS
acct_lf = f"{test_name}_acct_lf"
qos_lf = f"{test_name}_qos_lf"
acct_qos_combined_limit = f"{test_name}_acct_qos_combined_limit"
qos_combined_limit = f"{test_name}_qos_combined_limit"
acct_part_combined_limit = f"{test_name}_acct_part_combined_limit"
qos_part_lim = f"{test_name}_qos_part_lim"
qos_job_a = f"{test_name}_qos_ja"
qos_job_b = f"{test_name}_qos_jb"
# Duplicate same (assoc, job QoS, partition) on two het lines (tests 9d–9e)
acct_dup_accept = f"{test_name}_acct_dup_accept"
qos_dup_accept = f"{test_name}_qos_dup_accept"
acct_dup_reject = f"{test_name}_acct_dup_reject"
qos_dup_reject = f"{test_name}_qos_dup_reject"
# GrpTRES=node=2 vs running job + het (same assoc; queues until limit frees)
acct_grp_tres_run = f"{test_name}_acct_grp_tres_run"
qos_grp_tres_run = f"{test_name}_qos_grp_tres_run"

# Last Slurm (major, minor) treated as pre-fix: version checks use
# get_version() > SLURM_VERSION_HET_JOB_FIX for post-fix behavior (fix in 26.05+).
SLURM_VERSION_HET_JOB_FIX = (26, 4)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create partitions, gres, and accounting")
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/r1")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "r1")
    atf.require_nodes(4, [("Gres", "r1:2")])
    atf.require_config_parameter(
        "PartitionName",
        {
            p_cpu: {
                "Nodes": "node1,node2",
                "Default": "NO",
                "State": "UP",
            },
            p_gpu: {
                "Nodes": "node3,node4",
                "Default": "NO",
                "State": "UP",
            },
        },
    )
    atf.require_config_parameter_includes("SchedulerParameters", "bf_interval=1")
    atf.require_config_parameter_includes("SchedulerParameters", "sched_interval=1")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def setup_account_and_qos(setup):
    """Create one account and eight QoS (job-level and partition-level)."""
    atf.run_command(
        f"sacctmgr -i add qos {qos_cpu_deny} "
        f"flags=DenyOnLimit MaxtresPerUser=node=2",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_gpu_deny} "
        f"flags=DenyOnLimit MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_cpu_nodeny} " f"MaxtresPerUser=node=2",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_gpu_nodeny} " f"MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # Partition-level QoS (same limits; used as partition DefaultQOS)
    atf.run_command(
        f"sacctmgr -i add qos {qos_p_cpu_def_deny} "
        f"flags=DenyOnLimit MaxtresPerUser=node=2",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_p_gpu_def_deny} "
        f"flags=DenyOnLimit MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_p_cpu_def_nodeny} " f"MaxtresPerUser=node=2",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_p_gpu_def_nodeny} " f"MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    user = atf.get_user_name()
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct} "
        f"qos={qos_cpu_deny},{qos_gpu_deny},{qos_cpu_nodeny},{qos_gpu_nodeny},"
        f"{qos_p_cpu_def_deny},{qos_p_gpu_def_deny},"
        f"{qos_p_cpu_def_nodeny},{qos_p_gpu_def_nodeny}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del qos {qos_cpu_deny},{qos_gpu_deny},"
        f"{qos_cpu_nodeny},{qos_gpu_nodeny},"
        f"{qos_p_cpu_def_deny},{qos_p_gpu_def_deny},"
        f"{qos_p_cpu_def_nodeny},{qos_p_gpu_def_nodeny}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def setup_account_and_qos_tres(setup):
    """Account + QoS for Min/Max gres/r1 het: CPU max gres/r1=0, GPU min gres/r1=4."""
    atf.run_command(
        f"sacctmgr -i add qos {qos_tres_cpu_deny} "
        "flags=DenyOnLimit MaxtresPerJob=gres/r1=0",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_tres_gpu_deny} "
        "flags=DenyOnLimit MintresPerJob=gres/r1=4",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_tres_cpu_nodeny} MaxtresPerJob=gres/r1=0",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_tres_gpu_nodeny} MintresPerJob=gres/r1=4",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct_tres_gres}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    user = atf.get_user_name()
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_tres_gres} "
        f"qos={qos_tres_cpu_deny},{qos_tres_gpu_deny},"
        f"{qos_tres_cpu_nodeny},{qos_tres_gpu_nodeny}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del qos {qos_tres_cpu_deny},{qos_tres_gpu_deny},"
        f"{qos_tres_cpu_nodeny},{qos_tres_gpu_nodeny}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def setup_assoc_limit_het(setup):
    """
    One account with association GrpTRES=node=3; two QoS with high node limit.

    Used by tests 9–9c: both het components use the same association; a 2+2 node
    het stacks 4 nodes against GrpTRES=3 and is rejected at submit (tests 9–9b).
    """
    atf.run_command(
        f"sacctmgr -i add qos {qos_assoc_cpu} MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_assoc_gpu} MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct_assoc}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    user = atf.get_user_name()
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_assoc} "
        f"GrpTRES=node=3 qos={qos_assoc_cpu},{qos_assoc_gpu}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del qos {qos_assoc_cpu},{qos_assoc_gpu}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def setup_assoc_limit_het_two_accounts(setup):
    """
    Two accounts, each with association GrpTRES=node=3; two QoS with high node limit.

    Used by test_het_job_two_assoc_grp_tres_2plus2_accepted: each het component
    uses a different -A (2 nodes per account); each association only accrues its
    own 2 nodes against GrpTRES=3, so submit must succeed (unlike test 9 where one
    association sees 4 nodes).
    """
    atf.run_command(
        f"sacctmgr -i add qos {qos_assoc2_cpu} MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_assoc2_gpu} MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct_assoc2_a},{acct_assoc2_b}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    user = atf.get_user_name()
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_assoc2_a},{acct_assoc2_b} "
        f"GrpTRES=node=3 qos={qos_assoc2_cpu},{qos_assoc2_gpu}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del qos {qos_assoc2_cpu},{qos_assoc2_gpu}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def setup_assoc_grp_tres_running_contention(setup):
    """
    One account with association GrpTRES=node=2; QoS with high per-user limit.

    Used to verify that a running job charges GrpTRES so a subsequent het job
    whose stacked components exceed the remaining association capacity stays
    PENDING (mirrors salloc --account=X -N1 with salloc het : -N1 : -N1).
    """
    atf.run_command(
        f"sacctmgr -i add qos {qos_grp_tres_run} MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct_grp_tres_run}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    user = atf.get_user_name()
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_grp_tres_run} "
        f"GrpTRES=node=2 qos={qos_grp_tres_run}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del qos {qos_grp_tres_run}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def setup_limit_factor_het(setup):
    """
    Combined-limit test 10: association GrpTRES=node=10, one QoS with limit_factor 0.3 (effective 3).
    2+2 node het same assoc same QoS → stacked usage exceeds combined GrpTRES × limit_factor → rejected at submit (4 > 3).
    """
    clear_partition_default_qos()
    atf.run_command(
        f"sacctmgr -i add qos {qos_lf} flags=DenyOnLimit "
        f"LimitFactor=0.3 MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct_lf}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    user = atf.get_user_name()
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_lf} "
        f"GrpTRES=node=10 qos={qos_lf}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del qos {qos_lf}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def setup_qos_combined_limit_het(setup):
    """
    Combined-limit test 11: one QoS MaxTRESPerUser=node=3. 2+2 het same assoc same QoS,
    different partitions → stacked nodes in one (assoc, job QOS) group exceeds cap → rejected at submit (4 > 3).
    """
    clear_partition_default_qos()
    atf.run_command(
        f"sacctmgr -i add qos {qos_combined_limit} flags=DenyOnLimit "
        f"MaxtresPerUser=node=3",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct_qos_combined_limit}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    user = atf.get_user_name()
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_qos_combined_limit} "
        f"qos={qos_combined_limit}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del qos {qos_combined_limit}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def setup_partition_combined_limit_het(setup):
    """
    Combined-limit test 12: partition p_cpu QoS limit 1 node. 1+1 het same partition,
    different job QoS → stacked nodes in one (assoc, partition) group exceeds cap → rejected at submit (2 > 1).
    """
    atf.run_command(
        f"sacctmgr -i add qos {qos_part_lim} flags=DenyOnLimit "
        f"MaxtresPerUser=node=1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_job_a} MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {qos_job_b} MaxtresPerUser=node=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct_part_combined_limit}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    user = atf.get_user_name()
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_part_combined_limit} "
        f"qos={qos_job_a},{qos_job_b}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"scontrol update PartitionName={p_cpu} QoS={qos_part_lim}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del qos {qos_part_lim},{qos_job_a},{qos_job_b}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def setup_het_dup_group_accept(setup):
    """
    One account; one QoS with DenyOnLimit MaxTresPerUser=node=2.

    Used by test_het_job_duplicate_same_qos_partition_accept: two het components
    on p_cpu with the same --qos= (1+1 nodes in one assoc/job QoS/partition group).
    """
    clear_partition_default_qos()
    atf.run_command(
        f"sacctmgr -i add qos {qos_dup_accept} flags=DenyOnLimit "
        f"MaxtresPerUser=node=2",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct_dup_accept}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    user = atf.get_user_name()
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_dup_accept} "
        f"qos={qos_dup_accept}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del qos {qos_dup_accept}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture(scope="module")
def setup_het_dup_group_reject(setup):
    """
    One account; one QoS with DenyOnLimit MaxTresPerUser=node=1.

    Used by test_het_job_duplicate_same_qos_partition_rejected: same het shape as
    setup_het_dup_group_accept; stacked 1+1 in one group must be rejected at submit.
    """
    clear_partition_default_qos()
    atf.run_command(
        f"sacctmgr -i add qos {qos_dup_reject} flags=DenyOnLimit "
        f"MaxtresPerUser=node=1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct_dup_reject}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    user = atf.get_user_name()
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct_dup_reject} "
        f"qos={qos_dup_reject}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del qos {qos_dup_reject}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def _wait_job_pending_then_settled_or_done(job_id, observe_timeout=20):
    """
    Wait for PENDING, poll briefly for COMPLETED (exit early if job runs), then
    if RUNNING/COMPLETING wait for DONE. Replaces a fixed ~35s sleep while
    keeping the same allowed final states (PENDING or COMPLETED).
    """
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    atf.repeat_until(
        lambda: atf.get_job_parameter(job_id, "JobState", quiet=True),
        lambda s: s == "COMPLETED",
        timeout=observe_timeout,
        poll_interval=0.5,
        fatal=False,
    )
    state = atf.get_job_parameter(job_id, "JobState", quiet=True)
    if state in ("RUNNING", "COMPLETING", "CONFIGURING"):
        atf.wait_for_job_state(job_id, "DONE", timeout=120, fatal=True)
        state = atf.get_job_parameter(job_id, "JobState", quiet=True)
    return state


def set_partition_default_qos(qos_cpu, qos_gpu):
    """Set default QoS (QoS=) for p_cpu and p_gpu (used by partition-level tests)."""
    atf.run_command(
        f"scontrol update PartitionName={p_cpu} QoS={qos_cpu}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"scontrol update PartitionName={p_gpu} QoS={qos_gpu}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def scancel_started_job_leaders(*job_ids):
    """Cancel jobs by sbatch leader ids before cancel_all_jobs teardown.

    cancel_all_jobs passes every job id from scontrol JSON to bulk scancel; cancelling a het
    component id fails with rc=60. Cancelling leaders first removes the whole het job.
    """
    ids = [j for j in job_ids if j]
    if not ids:
        return
    atf.cancel_jobs(
        ids,
        fatal=False,
        quiet=True,
        user=atf.properties["slurm-user"],
    )


def clear_partition_default_qos():
    """Clear partition QoS= on p_cpu and p_gpu (called from cancel_jobs teardown and combined-limit fixtures)."""
    atf.run_command(
        f"scontrol update PartitionName={p_cpu} QoS=",
        user=atf.properties["slurm-user"],
        fatal=False,
    )
    atf.run_command(
        f"scontrol update PartitionName={p_gpu} QoS=",
        user=atf.properties["slurm-user"],
        fatal=False,
    )


@pytest.fixture(scope="function")
def cancel_jobs():
    """Cancel all jobs after each test; clear partition QoS= (no leaking DefaultQOS)."""
    yield
    atf.cancel_all_jobs(fatal=True)
    clear_partition_default_qos()


def test_het_job_without_deny_on_limit_accepted(setup_account_and_qos, cancel_jobs):
    """
    No DenyOnLimit; same het shape (1+2 nodes). Submit succeeds; job may be PENDING or COMPLETED.
    """
    atf.make_bash_script(
        "het_nodeny.in",
        f"""
#SBATCH -p {p_cpu} --qos={qos_cpu_nodeny} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_gpu_nodeny} -N2 -t1
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_nodeny.in", fatal=False)
    assert job_id != 0, "Without DenyOnLimit, het job should be accepted at submit"
    state = _wait_job_pending_then_settled_or_done(job_id)
    assert state in (
        "PENDING",
        "COMPLETED",
    ), f"Job should be PENDING (post-fix) or COMPLETED (pre-fix). Got {state}"


def test_het_job_pending_when_gpu_partition_busy(setup_account_and_qos, cancel_jobs):
    """
    Plain job holds both p_gpu nodes; het needs 1 on p_cpu and 2 on p_gpu (same shape as het_nodeny).
    Submit validation passes but all-or-nothing scheduling cannot place the GPU component → het stays
    PENDING until resources free. Scheduler contention, not assoc/QoS submit validation.
    """
    block_jid = atf.submit_job_sbatch(
        f"-J {test_name}_gpu_all -p {p_gpu} --qos={qos_gpu_nodeny} "
        '-N2 -t10 --wrap "sleep 600"',
        fatal=False,
    )
    assert block_jid != 0, "Blocker job should submit"
    atf.wait_for_job_state(block_jid, "RUNNING", timeout=120, fatal=True)

    atf.make_bash_script(
        "het_gpu_busy.in",
        f"""
#SBATCH -p {p_cpu} --qos={qos_cpu_nodeny} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_gpu_nodeny} -N2 -t1
sleep 1
""",
    )
    het_jid = atf.submit_job_sbatch("het_gpu_busy.in", fatal=False)
    assert het_jid != 0, "Het job should be accepted at submit"

    atf.repeat_until(
        lambda: atf.get_job_parameter(het_jid, "JobState", quiet=True),
        lambda s: s == "PENDING",
        timeout=60,
        poll_interval=0.5,
        fatal=True,
    )
    assert (
        atf.get_job_parameter(block_jid, "JobState", quiet=True) == "RUNNING"
    ), "Blocker should still be running while het remains pending"

    time.sleep(2)
    assert (
        atf.get_job_parameter(het_jid, "JobState", quiet=True) == "PENDING"
    ), "Het leader should stay PENDING while both GPU nodes are consumed"

    scancel_started_job_leaders(block_jid, het_jid)


# --- Partition-level QoS (DefaultQOS; no --qos= in script) ---


def test_partition_level_nodeny_pending(setup_account_and_qos, cancel_jobs):
    """
    Partition DefaultQOS, no DenyOnLimit; same het shape as het_part_nodeny. Submit succeeds;
    job may be PENDING or COMPLETED.
    """
    set_partition_default_qos(qos_p_cpu_def_nodeny, qos_p_gpu_def_nodeny)
    atf.make_bash_script(
        "het_part_nodeny.in",
        f"""
#SBATCH -p {p_cpu} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} -N2 -t1
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_part_nodeny.in", fatal=False)
    assert (
        job_id != 0
    ), "Partition-level without DenyOnLimit, het job should be accepted"
    state = _wait_job_pending_then_settled_or_done(job_id)
    assert state in (
        "PENDING",
        "COMPLETED",
    ), f"Job should be PENDING (post-fix) or COMPLETED (pre-fix). Got {state}"


# --- Post-fix behavior: skipif on Slurm < 26.05 ---


@pytest.mark.skipif(
    atf.get_version() <= SLURM_VERSION_HET_JOB_FIX,
    reason="Test case invalid for the current Slurm version (requires Slurm >= 26.05).",
)
def test_het_job_with_deny_on_limit_accepted_and_runs(
    setup_account_and_qos, cancel_jobs
):
    """
    DenyOnLimit; job-level --qos=. Slurm >= 26.05: het (1+2 nodes) accepted at submit and completes.
    """
    atf.make_bash_script(
        "het_deny.in",
        f"""
#SBATCH -p {p_cpu} --qos={qos_cpu_deny} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_gpu_deny} -N2 -t1
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_deny.in", fatal=False)
    assert job_id != 0, (
        "With DenyOnLimit, het job (1 node CPU + 2 nodes GPU) should be "
        "accepted at submit after 24603 fix (TRES per assoc/qos/partition)"
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)


def test_het_job_without_deny_on_limit_accepted_and_runs(
    setup_account_and_qos, cancel_jobs
):
    """
    No DenyOnLimit; same 1+2 het. Submit succeeds; job runs to completion (no-deny path on all versions).
    """
    atf.make_bash_script(
        "het_nodeny.in",
        f"""
#SBATCH -p {p_cpu} --qos={qos_cpu_nodeny} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_gpu_nodeny} -N2 -t1
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_nodeny.in", fatal=False)
    assert job_id != 0, (
        "Without DenyOnLimit, het job (1 node CPU + 2 nodes GPU) should be "
        "accepted at submit"
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)


@pytest.mark.skipif(
    atf.get_version() <= SLURM_VERSION_HET_JOB_FIX,
    reason="Test case invalid for the current Slurm version (requires Slurm >= 26.05).",
)
def test_partition_level_deny_accepted_and_runs(setup_account_and_qos, cancel_jobs):
    """
    Partition DefaultQOS + DenyOnLimit. Slurm >= 26.05: het accepted and completes (per-group limits).
    """
    set_partition_default_qos(qos_p_cpu_def_deny, qos_p_gpu_def_deny)
    atf.make_bash_script(
        "het_part_deny.in",
        f"""
#SBATCH -p {p_cpu} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} -N2 -t1
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_part_deny.in", fatal=False)
    assert (
        job_id != 0
    ), "Partition-level DenyOnLimit het job should be accepted after 24603 fix"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)


def test_partition_level_nodeny_accepted_and_runs(setup_account_and_qos, cancel_jobs):
    """
    Partition DefaultQOS, no DenyOnLimit. Submit succeeds; job runs to completion (no-deny path on all versions).
    """
    set_partition_default_qos(qos_p_cpu_def_nodeny, qos_p_gpu_def_nodeny)
    atf.make_bash_script(
        "het_part_nodeny.in",
        f"""
#SBATCH -p {p_cpu} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} -N2 -t1
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_part_nodeny.in", fatal=False)
    assert job_id != 0, "Partition-level without DenyOnLimit het job should be accepted"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)


# --- Job-level Min/Max TRES (gres/r1) per component ---


def test_tres_nodeny_pending(setup_account_and_qos_tres, cancel_jobs):
    """
    No DenyOnLimit; same Min/Max gres/r1 het. Submit succeeds; job may be PENDING or COMPLETED.
    """
    atf.make_bash_script(
        "het_tres_nodeny.in",
        f"""
#SBATCH -A {acct_tres_gres}
#SBATCH -p {p_cpu} --qos={qos_tres_cpu_nodeny} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_tres_gpu_nodeny} -N2 -t1 --gres=r1:2
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_tres_nodeny.in", fatal=False)
    assert (
        job_id != 0
    ), "Min/Max TRES het job without DenyOnLimit should be accepted at submit"
    state = _wait_job_pending_then_settled_or_done(job_id)
    assert state in (
        "PENDING",
        "COMPLETED",
    ), f"Job should be PENDING (post-fix) or COMPLETED (pre-fix). Got {state}"


@pytest.mark.skipif(
    atf.get_version() <= SLURM_VERSION_HET_JOB_FIX,
    reason="Test case invalid for the current Slurm version (requires Slurm >= 26.05).",
)
def test_tres_deny_accepted_and_runs(setup_account_and_qos_tres, cancel_jobs):
    """
    DenyOnLimit; per-component gres/r1 (0 + 4 r1). Slurm >= 26.05: accepted and completes (each component
    satisfies its QoS).
    """
    atf.make_bash_script(
        "het_tres_deny.in",
        f"""
#SBATCH -A {acct_tres_gres}
#SBATCH -p {p_cpu} --qos={qos_tres_cpu_deny} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_tres_gpu_deny} -N2 -t1 --gres=r1:2
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_tres_deny.in", fatal=False)
    assert (
        job_id != 0
    ), "Post-fix: Min/Max TRES het job with DenyOnLimit should be accepted"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)


def test_tres_nodeny_accepted_and_runs(setup_account_and_qos_tres, cancel_jobs):
    """
    No DenyOnLimit; same Min/Max gres/r1 het. Submit succeeds; job runs to completion (no-deny path on all versions).
    """
    atf.make_bash_script(
        "het_tres_nodeny.in",
        f"""
#SBATCH -A {acct_tres_gres}
#SBATCH -p {p_cpu} --qos={qos_tres_cpu_nodeny} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_tres_gpu_nodeny} -N2 -t1 --gres=r1:2
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_tres_nodeny.in", fatal=False)
    assert job_id != 0, "Min/Max TRES het job without DenyOnLimit should be accepted"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)


@pytest.mark.skipif(
    atf.get_version() <= SLURM_VERSION_HET_JOB_FIX,
    reason="Test case invalid for the current Slurm version (requires Slurm >= 26.05).",
)
def test_het_job_assoc_total_over_limit_rejected(setup_assoc_limit_het, cancel_jobs):
    """
    GrpTRES=node=3 on one association; job-level --qos=. Slurm >= 26.05: single het 2+2 stacks 4 > 3 → rejected at submit.
    """
    atf.make_bash_script(
        "het_assoc_total.in",
        f"""
#SBATCH -A {acct_assoc}
#SBATCH -p {p_cpu} --qos={qos_assoc_cpu} -N2 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_assoc_gpu} -N2 -t1
sleep 1
""",
    )
    assert atf.submit_job_sbatch("het_assoc_total.in", fatal=False) == 0, (
        "Het job (2+2 nodes) with GrpTRES=node=3 on the shared association should "
        "be rejected at submit (4 > 3)."
    )


def test_het_job_two_assoc_grp_tres_2plus2_accepted(
    setup_assoc_limit_het_two_accounts, cancel_jobs
):
    """
    GrpTRES=node=3 per association; different -A per het component (2+2 nodes total). Each association
    sees 2 ≤ 3 at submit—not four nodes on one association. Submit succeeds; job may be PENDING or COMPLETED.
    """
    atf.make_bash_script(
        "het_assoc_two_acct.in",
        f"""
#SBATCH -A {acct_assoc2_a}
#SBATCH -p {p_cpu} --qos={qos_assoc2_cpu} -N2 -t1
#SBATCH hetjob
#SBATCH -A {acct_assoc2_b}
#SBATCH -p {p_gpu} --qos={qos_assoc2_gpu} -N2 -t1
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_assoc_two_acct.in", fatal=False)
    assert job_id != 0, (
        "Het job (2+2 nodes) with GrpTRES=node=3 on two different associations "
        "should be accepted at submit (2 per association, not 4 on one)."
    )
    state = _wait_job_pending_then_settled_or_done(job_id)
    assert state in (
        "PENDING",
        "COMPLETED",
    ), f"Job should be PENDING or COMPLETED. Got {state}"


@pytest.mark.skipif(
    atf.get_version() <= SLURM_VERSION_HET_JOB_FIX,
    reason="Test case invalid for the current Slurm version (requires Slurm >= 26.05).",
)
def test_het_job_grp_tres_pend_with_running_alloc_same_account(
    setup_assoc_grp_tres_running_contention, cancel_jobs
):
    """
    GrpTRES=node=2; QoS MaxTresPerUser=node=10. One-node blocker runs on p_cpu (--exclusive); a 1+1 het on
    the same account/QoS/partition cannot start until that allocation frees capacity under the group cap.
    Het is accepted at submit and stays PENDING—not submit-time rejection.
    """
    block_jid = atf.submit_job_sbatch(
        f"-J {test_name}_grp_tres_block -A {acct_grp_tres_run} "
        f"-p {p_cpu} --qos={qos_grp_tres_run} -N1 --exclusive -t10 "
        '--wrap "sleep 600"',
        fatal=False,
    )
    assert block_jid != 0, "Blocker job should submit"
    atf.wait_for_job_state(block_jid, "RUNNING", timeout=120, fatal=True)

    atf.make_bash_script(
        "het_grp_tres_contend.in",
        f"""
#SBATCH -A {acct_grp_tres_run}
#SBATCH -J {test_name}_het_grptres
#SBATCH -p {p_cpu} --qos={qos_grp_tres_run} -N1 -t1 --exclusive
#SBATCH hetjob
#SBATCH -p {p_cpu} --qos={qos_grp_tres_run} -N1 -t1 --exclusive
sleep 1
""",
    )
    het_jid = atf.submit_job_sbatch("het_grp_tres_contend.in", fatal=False)
    assert het_jid != 0, "Het job should be accepted at submit (under GrpTRES cap)"

    atf.repeat_until(
        lambda: atf.get_job_parameter(het_jid, "JobState", quiet=True),
        lambda s: s == "PENDING",
        timeout=60,
        poll_interval=0.5,
        fatal=True,
    )
    assert (
        atf.get_job_parameter(block_jid, "JobState", quiet=True) == "RUNNING"
    ), "Blocker should still be running while het remains pending"

    time.sleep(2)
    assert atf.get_job_parameter(het_jid, "JobState", quiet=True) == "PENDING", (
        "Het job should stay PENDING: association GrpTRES=node=2 with one node "
        "in use by the running alloc and two nodes needed for het components."
    )

    scancel_started_job_leaders(block_jid, het_jid)


@pytest.mark.skipif(
    atf.get_version() <= SLURM_VERSION_HET_JOB_FIX,
    reason="Test case invalid for the current Slurm version (requires Slurm >= 26.05).",
)
def test_het_job_grp_tres_runs_after_running_alloc_completes_same_account(
    setup_assoc_grp_tres_running_contention, cancel_jobs
):
    """
    Same limits as test_het_job_grp_tres_pend_with_running_alloc_same_account (GrpTRES=node=2); blocker uses a
    short sleep. Het queues while the blocker runs; after blocker DONE, het runs to completion.
    """
    block_jid = atf.submit_job_sbatch(
        f"-J {test_name}_grp_tres_block_short -A {acct_grp_tres_run} "
        f"-p {p_cpu} --qos={qos_grp_tres_run} -N1 --exclusive -t2 "
        '--wrap "sleep 10"',
        fatal=False,
    )
    assert block_jid != 0, "Blocker job should submit"
    atf.wait_for_job_state(block_jid, "RUNNING", timeout=120, fatal=True)

    atf.make_bash_script(
        "het_grp_tres_after_block.in",
        f"""
#SBATCH -A {acct_grp_tres_run}
#SBATCH -J {test_name}_het_grptres_after
#SBATCH -p {p_cpu} --qos={qos_grp_tres_run} -N1 -t1 --exclusive
#SBATCH hetjob
#SBATCH -p {p_cpu} --qos={qos_grp_tres_run} -N1 -t1 --exclusive
sleep 1
""",
    )
    het_jid = atf.submit_job_sbatch("het_grp_tres_after_block.in", fatal=False)
    assert het_jid != 0, "Het job should be accepted at submit (under GrpTRES cap)"

    atf.repeat_until(
        lambda: atf.get_job_parameter(het_jid, "JobState", quiet=True),
        lambda s: s == "PENDING",
        timeout=60,
        poll_interval=0.5,
        fatal=True,
    )
    assert (
        atf.get_job_parameter(block_jid, "JobState", quiet=True) == "RUNNING"
    ), "Blocker should still be running while het is pending"

    atf.wait_for_job_state(block_jid, "DONE", timeout=120, fatal=True)
    atf.wait_for_job_state(het_jid, "DONE", timeout=120, fatal=True)


@pytest.mark.skipif(
    atf.get_version() <= SLURM_VERSION_HET_JOB_FIX,
    reason="Test case invalid for the current Slurm version (requires Slurm >= 26.05).",
)
def test_het_job_grp_tres_three_running_second_smaller_pends_same_account(
    setup_assoc_limit_het, cancel_jobs
):
    """
    GrpTRES=node=3: first het uses three nodes (1 exclusive on p_cpu + 2 exclusive on p_gpu), saturating
    the association group. A second het (1+1) is under the per-submit GrpTRES ceiling (2 ≤ 3) but cannot
    start while the first het holds group node usage; it should be accepted at submit and stay PENDING—not
    submit-time rejection.
    """
    atf.make_bash_script(
        "het_grp3_sat_first.in",
        f"""
#SBATCH -A {acct_assoc}
#SBATCH -J {test_name}_grp3_sat_first
#SBATCH -p {p_cpu} --qos={qos_assoc_cpu} -N1 -t10 --exclusive
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_assoc_gpu} -N2 -t10 --exclusive
sleep 600
""",
    )
    first_jid = atf.submit_job_sbatch("het_grp3_sat_first.in", fatal=False)
    assert first_jid != 0, "First het job (1+2 nodes) should be accepted at submit"
    atf.wait_for_job_state(first_jid, "RUNNING", timeout=120, fatal=True)

    atf.make_bash_script(
        "het_grp3_sat_second.in",
        f"""
#SBATCH -A {acct_assoc}
#SBATCH -J {test_name}_grp3_sat_second
#SBATCH -p {p_cpu} --qos={qos_assoc_cpu} -N1 -t1 --exclusive
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_assoc_gpu} -N1 -t1 --exclusive
sleep 1
""",
    )
    second_jid = atf.submit_job_sbatch("het_grp3_sat_second.in", fatal=False)
    assert (
        second_jid != 0
    ), "Second het (1+1 nodes) should be accepted at submit while first het is running"

    atf.repeat_until(
        lambda: atf.get_job_parameter(second_jid, "JobState", quiet=True),
        lambda s: s == "PENDING",
        timeout=60,
        poll_interval=0.5,
        fatal=True,
    )
    assert (
        atf.get_job_parameter(first_jid, "JobState", quiet=True) == "RUNNING"
    ), "First het should still be running while second het is pending"

    time.sleep(2)
    assert atf.get_job_parameter(second_jid, "JobState", quiet=True) == "PENDING", (
        "Second het should stay PENDING: association GrpTRES=node=3 is saturated "
        "by the running first het (1+2 nodes); the follow-up het cannot start yet."
    )

    scancel_started_job_leaders(first_jid, second_jid)


@pytest.mark.skipif(
    atf.get_version() <= SLURM_VERSION_HET_JOB_FIX,
    reason="Test case invalid for the current Slurm version (requires Slurm >= 26.05).",
)
def test_het_job_assoc_total_over_limit_rejected_partition_default_qos(
    setup_assoc_limit_het, cancel_jobs
):
    """
    GrpTRES=node=3; partition DefaultQOS (no job --qos=). Slurm >= 26.05: single het 2+2 stacks 4 > 3 → rejected at submit.
    """
    set_partition_default_qos(qos_assoc_cpu, qos_assoc_gpu)
    atf.make_bash_script(
        "het_assoc_total_partqos.in",
        f"""
#SBATCH -A {acct_assoc}
#SBATCH -p {p_cpu} -N2 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} -N2 -t1
sleep 1
""",
    )
    assert atf.submit_job_sbatch("het_assoc_total_partqos.in", fatal=False) == 0, (
        "Het job (2+2 nodes, partition DefaultQOS) with GrpTRES=node=3 on the "
        "shared association should be rejected at submit (4 > 3)."
    )


def test_het_job_assoc_total_same_partition_default_qos(
    setup_assoc_limit_het, cancel_jobs
):
    """
    GrpTRES=node=3; partition DefaultQOS; both het components on p_cpu (1+1 nodes, 2 ≤ 3). Submit succeeds;
    job may be PENDING or COMPLETED.
    """
    set_partition_default_qos(qos_assoc_cpu, qos_assoc_gpu)
    atf.make_bash_script(
        "het_assoc_total_samepart.in",
        f"""
#SBATCH -A {acct_assoc}
#SBATCH -p {p_cpu} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_cpu} -N1 -t1
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_assoc_total_samepart.in", fatal=False)
    assert job_id != 0, (
        "Same partition + partition DefaultQOS: 1+1 het on p_cpu with "
        "GrpTRES=node=3 should be accepted (2 nodes in one assoc/qos/part group)."
    )
    state = _wait_job_pending_then_settled_or_done(job_id)
    assert state in (
        "PENDING",
        "COMPLETED",
    ), f"Job should be PENDING or COMPLETED. Got {state}"


def test_het_job_duplicate_same_qos_partition_accept(
    setup_het_dup_group_accept, cancel_jobs
):
    """
    DenyOnLimit; duplicate (assoc, job QoS, partition) on p_cpu; MaxTresPerUser=node=2; 1+1 het stacks
    2 ≤ 2 in one group → accepted at submit; job may be PENDING or COMPLETED.
    """
    atf.make_bash_script(
        "het_dup_qos_accept.in",
        f"""
#SBATCH -A {acct_dup_accept}
#SBATCH -p {p_cpu} --qos={qos_dup_accept} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_cpu} --qos={qos_dup_accept} -N1 -t1
sleep 1
""",
    )
    job_id = atf.submit_job_sbatch("het_dup_qos_accept.in", fatal=False)
    assert job_id != 0, (
        "Duplicate (assoc, qos, partition): 1+1 nodes with MaxTresPerUser=2 "
        "should be accepted at submit."
    )
    state = _wait_job_pending_then_settled_or_done(job_id)
    assert state in (
        "PENDING",
        "COMPLETED",
    ), f"Job should be PENDING or COMPLETED. Got {state}"


def test_het_job_duplicate_same_qos_partition_rejected(
    setup_het_dup_group_reject, cancel_jobs
):
    """
    DenyOnLimit; duplicate same group on p_cpu; MaxTresPerUser=node=1; 1+1 stacks 2 > 1 → rejected at submit.
    """
    atf.make_bash_script(
        "het_dup_qos_reject.in",
        f"""
#SBATCH -A {acct_dup_reject}
#SBATCH -p {p_cpu} --qos={qos_dup_reject} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_cpu} --qos={qos_dup_reject} -N1 -t1
sleep 1
""",
    )
    assert atf.submit_job_sbatch("het_dup_qos_reject.in", fatal=False) == 0, (
        "Duplicate (assoc, qos, partition): 1+1 nodes with MaxTresPerUser=1 "
        "should be rejected at submit (stacked 2 > 1)."
    )


# --- Combined-limit: limit_factor, (assoc,qos), (assoc,partition) rejection (tests 10–12) ---


def test_het_job_limit_factor_combined_limit_rejected(
    setup_limit_factor_het, cancel_jobs
):
    """
    Association GrpTRES=node=10; QoS limit_factor=0.3 (effective node cap 3). Het 2+2 same assoc and job QoS
    stacks 4 > 3 → rejected at submit.
    """
    atf.make_bash_script(
        "het_lf.in",
        f"""
#SBATCH -A {acct_lf}
#SBATCH -p {p_cpu} --qos={qos_lf} -N2 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_lf} -N2 -t1
sleep 1
""",
    )
    assert atf.submit_job_sbatch("het_lf.in", fatal=False) == 0, (
        "Combined-limit: het job (2+2 nodes) with assoc GrpTRES=node=10 and QoS "
        "limit_factor=0.3 (effective 3) should be rejected at submit."
    )


def test_het_job_qos_combined_limit_rejected(setup_qos_combined_limit_het, cancel_jobs):
    """
    QoS MaxTresPerUser=node=3; het 2+2 across partitions with same assoc and job QoS stacks 4 > 3 in one
    bucket → rejected at submit.
    """
    atf.make_bash_script(
        "het_qos_combined_limit.in",
        f"""
#SBATCH -A {acct_qos_combined_limit}
#SBATCH -p {p_cpu} --qos={qos_combined_limit} -N2 -t1
#SBATCH hetjob
#SBATCH -p {p_gpu} --qos={qos_combined_limit} -N2 -t1
sleep 1
""",
    )
    assert atf.submit_job_sbatch("het_qos_combined_limit.in", fatal=False) == 0, (
        "Combined-limit: het job (2+2 nodes) with QoS MaxTresPerUser=node=3 should be "
        "rejected at submit when stacked TRES for (assoc, job QOS) exceeds the cap."
    )


def test_het_job_partition_combined_limit_rejected(
    setup_partition_combined_limit_het, cancel_jobs
):
    """
    Partition p_cpu QoS max node=1; 1+1 het in the same partition with different job QoS stacks 2 > 1 in
    the assoc+partition bucket → rejected at submit.
    """
    atf.make_bash_script(
        "het_part_combined_limit.in",
        f"""
#SBATCH -A {acct_part_combined_limit}
#SBATCH -p {p_cpu} --qos={qos_job_a} -N1 -t1
#SBATCH hetjob
#SBATCH -p {p_cpu} --qos={qos_job_b} -N1 -t1
sleep 1
""",
    )
    assert atf.submit_job_sbatch("het_part_combined_limit.in", fatal=False) == 0, (
        "Combined-limit: het job (1+1 nodes in same partition) with partition QoS "
        "limit 1 should be rejected at submit when stacked TRES for (assoc, partition) exceeds the cap."
    )
